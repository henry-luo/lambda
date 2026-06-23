/**
 * js_net.cpp — Node.js-style 'net' module for LambdaJS
 *
 * Provides net.createServer() and net.createConnection() backed by libuv TCP.
 * Registered as built-in module 'net' via js_module_get().
 */
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_event_loop.h"
#include "js_class.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"

#include <cstdlib>
#include <cstring>

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

static Item make_undefined_item(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static bool is_undefined_item(Item item) {
    return item.item == ITEM_JS_UNDEFINED || get_type_id(item) == LMD_TYPE_UNDEFINED;
}

static bool is_callable(Item item) {
    return get_type_id(item) == LMD_TYPE_FUNC;
}

// =============================================================================
// Socket — wraps uv_tcp_t with event model
// =============================================================================

typedef struct JsSocket {
    uv_tcp_t tcp;
    Item      js_object;     // the JS object representing this socket
    bool      connected;
    bool      destroyed;
    bool      connect_pending;
    bool      is_server_side;
    bool      reading;
    bool      finished;
    int64_t   bytes_read;
    int64_t   bytes_written;
    int64_t   buffer_size;
} JsSocket;

typedef struct SocketWriteReq {
    uv_write_t req;
    char*      data;
    size_t     len;
    JsSocket*  sock;
    Item       callback;
} SocketWriteReq;

static JsSocket* socket_from_object(Item self) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL || is_undefined_item(handle_item)) return NULL;
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (JsSocket*)(uintptr_t)it2i(handle_item);
}

static void socket_set_listener(Item obj, const char* event, Item callback) {
    if (!is_callable(callback)) return;
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    js_property_set(obj, make_string_item(key), callback);
}

static void socket_close_now(JsSocket* sock);
static Item make_uv_error(int status, const char* syscall, const char* host, int port);

static void socket_update_io_counters(JsSocket* sock) {
    if (!sock) return;
    js_property_set(sock->js_object, make_string_item("bytesRead"),
                    (Item){.item = i2it(sock->bytes_read)});
    js_property_set(sock->js_object, make_string_item("bytesWritten"),
                    (Item){.item = i2it(sock->bytes_written)});
    js_property_set(sock->js_object, make_string_item("bufferSize"),
                    (Item){.item = i2it(sock->buffer_size)});
}

// emit event on socket JS object
static void socket_emit(Item obj, const char* event, Item* args, int argc) {
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item cb = js_property_get(obj, make_string_item(key));
    if (is_callable(cb)) {
        js_call_function(cb, obj, args, argc);
        js_microtask_flush();
    }
}

static void socket_pipe_data(Item obj, Item data) {
    Item dest = js_property_get(obj, make_string_item("__pipe_dest__"));
    if (dest.item == 0 || dest.item == ITEM_NULL || is_undefined_item(dest)) return;
    Item write_fn = js_property_get(dest, make_string_item("write"));
    if (is_callable(write_fn)) {
        js_call_function(write_fn, dest, &data, 1);
        js_microtask_flush();
    }
}

static void socket_pipe_end(Item obj) {
    Item dest = js_property_get(obj, make_string_item("__pipe_dest__"));
    if (dest.item == 0 || dest.item == ITEM_NULL || is_undefined_item(dest)) return;
    Item end_fn = js_property_get(dest, make_string_item("end"));
    if (is_callable(end_fn)) {
        js_call_function(end_fn, dest, NULL, 0);
        js_microtask_flush();
    }
}

static void socket_emit_finish_once(JsSocket* sock) {
    if (!sock || sock->finished) return;
    sock->finished = true;
    socket_emit(sock->js_object, "finish", NULL, 0);
}

static bool socket_allow_half_open(Item obj) {
    Item value = js_property_get(obj, make_string_item("allowHalfOpen"));
    return get_type_id(value) == LMD_TYPE_BOOL && it2b(value);
}

static void socket_finish_on_remote_end(JsSocket* sock) {
    if (!sock || socket_allow_half_open(sock->js_object)) return;
    socket_emit_finish_once(sock);
}

static bool socket_get_write_bytes(Item item, const char** out_data, size_t* out_len) {
    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* s = it2s(item);
        *out_data = s->chars;
        *out_len = s->len;
        return true;
    }
    if (js_is_typed_array(item)) {
        if (js_typed_array_is_out_of_bounds_item(item)) return false;
        int byte_len = js_typed_array_byte_length(item);
        void* data = js_typed_array_current_data_ptr(item);
        if (byte_len > 0 && !data) return false;
        *out_data = (const char*)data;
        *out_len = (size_t)byte_len;
        return true;
    }
    return false;
}

// on(event, callback) — store as __on_<event>__
extern "C" Item js_socket_on(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    return self;
}

static Item socket_write_data(Item self, JsSocket* sock, Item data_item, Item callback) {
    if (!sock || sock->destroyed) return (Item){.item = b2it(false)};

    const char* data = NULL;
    size_t data_len = 0;
    if (!socket_get_write_bytes(data_item, &data, &data_len)) {
        return (Item){.item = b2it(false)};
    }

    SocketWriteReq* wreq = (SocketWriteReq*)mem_calloc(1, sizeof(SocketWriteReq), MEM_CAT_JS_RUNTIME);
    // copy data since it may be GC'd
    char* copy = (char*)mem_alloc(data_len, MEM_CAT_JS_RUNTIME);
    memcpy(copy, data, data_len);
    uv_buf_t buf = uv_buf_init(copy, (unsigned int)data_len);
    wreq->data = copy;
    wreq->len = data_len;
    wreq->sock = sock;
    wreq->callback = callback;
    wreq->req.data = wreq;

    sock->bytes_written += (int64_t)data_len;
    sock->buffer_size += (int64_t)data_len;
    socket_update_io_counters(sock);

    int r = uv_write(&wreq->req, (uv_stream_t*)&sock->tcp, &buf, 1,
        [](uv_write_t* req, int status) {
            SocketWriteReq* wreq = (SocketWriteReq*)req->data;
            if (!wreq) return;
            if (wreq->sock) {
                wreq->sock->buffer_size -= (int64_t)wreq->len;
                if (wreq->sock->buffer_size < 0) wreq->sock->buffer_size = 0;
                socket_update_io_counters(wreq->sock);
            }
            if (is_callable(wreq->callback)) {
                if (status == 0) {
                    js_call_function(wreq->callback, make_undefined_item(), NULL, 0);
                } else {
                    Item err = make_uv_error(status, "write", NULL, -1);
                    js_call_function(wreq->callback, make_undefined_item(), &err, 1);
                }
                js_microtask_flush();
            }
            if (wreq->data) mem_free(wreq->data);
            mem_free(wreq);
        });

    if (r != 0) {
        sock->bytes_written -= (int64_t)data_len;
        sock->buffer_size -= (int64_t)data_len;
        if (sock->bytes_written < 0) sock->bytes_written = 0;
        if (sock->buffer_size < 0) sock->buffer_size = 0;
        socket_update_io_counters(sock);
        mem_free(copy);
        mem_free(wreq);
        return (Item){.item = b2it(false)};
    }

    return (Item){.item = b2it(true)};
}

// write(data[, callback]) — write to socket
extern "C" Item js_socket_write(Item data_item, Item callback) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    return socket_write_data(self, sock, data_item, callback);
}

// end() — half-close (shutdown write side)
extern "C" Item js_socket_end(Item data_item) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (!is_undefined_item(data_item) && data_item.item != ITEM_NULL) {
        socket_write_data(self, sock, data_item, make_undefined_item());
    }
    if (!sock || sock->destroyed) return self;

    uv_shutdown_t* sreq = (uv_shutdown_t*)mem_calloc(1, sizeof(uv_shutdown_t), MEM_CAT_JS_RUNTIME);
    sreq->data = sock;
    uv_shutdown(sreq, (uv_stream_t*)&sock->tcp,
        [](uv_shutdown_t* req, int status) {
            JsSocket* sock = (JsSocket*)req->data;
            if (sock && !sock->destroyed) {
                socket_emit_finish_once(sock);
            }
            mem_free(req);
        });

    return self;
}

// destroy() — close socket
extern "C" Item js_socket_destroy(Item error_item) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (!sock || sock->destroyed) return self;

    sock->destroyed = true;
    js_property_set(self, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
    if (!is_undefined_item(error_item) && error_item.item != ITEM_NULL) {
        socket_emit(self, "error", &error_item, 1);
    }
    if (sock->connect_pending) return self;
    socket_close_now(sock);
    return self;
}

static void socket_close_now(JsSocket* sock) {
    if (!sock) return;
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        sock->reading = false;
        uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
            JsSocket* s = (JsSocket*)handle->data;
            if (s) {
                socket_emit(s->js_object, "close", NULL, 0);
                mem_free(s);
            }
        });
    }
}

// Socket.setTimeout(msecs, callback) — set idle timeout
static Item js_socket_setTimeout(Item msecs, Item callback) {
    Item self = js_get_this();
    // Store timeout value; if callback provided, add as 'timeout' listener
    js_property_set(self, make_string_item("timeout"), msecs);
    if (is_callable(callback)) {
        // Register 'timeout' listener
        Item args[] = { make_string_item("timeout"), callback };
        Item on_fn = js_property_get(self, make_string_item("on"));
        if (is_callable(on_fn)) {
            js_call_function(on_fn, self, args, 2);
        }
    }
    return self;
}

static Item js_socket_connect_args(Item self, Item rest_args);

// Socket.connect(options/port, host, callback) — initiate connection
static Item js_socket_connect(Item rest_args) {
    Item self = js_get_this();
    return js_socket_connect_args(self, rest_args);
}

// Socket.setKeepAlive(enable, initialDelay) — stub
static Item js_socket_setKeepAlive(Item enable, Item delay) {
    return js_get_this();
}

// Socket.setNoDelay(noDelay) — stub
static Item js_socket_setNoDelay(Item noDelay) {
    return js_get_this();
}

// Socket.ref() / Socket.unref() — stub
static Item js_socket_ref(void) { return js_get_this(); }
static Item js_socket_unref(void) { return js_get_this(); }
static Item js_socket_cork(void) { return js_get_this(); }
static Item js_socket_uncork(void) { return js_get_this(); }

static Item js_socket_setEncoding(Item encoding) {
    Item self = js_get_this();
    js_property_set(self, make_string_item("__encoding__"), encoding);
    return self;
}

static Item js_socket_pipe(Item dest) {
    Item self = js_get_this();
    js_property_set(self, make_string_item("__pipe_dest__"), dest);
    return dest;
}

static void client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

static bool socket_start_read(JsSocket* sock) {
    if (!sock || sock->destroyed || sock->reading) return false;
    if (uv_is_closing((uv_handle_t*)&sock->tcp)) return false;

    uv_alloc_cb alloc_cb = sock->is_server_side ? server_alloc_cb : client_alloc_cb;
    uv_read_cb read_cb = sock->is_server_side ? server_client_read_cb : client_read_cb;
    int r = uv_read_start((uv_stream_t*)&sock->tcp, alloc_cb, read_cb);
    if (r == 0) sock->reading = true;
    return r == 0;
}

// Socket.resume() — restart libuv reads after pause().
static Item js_socket_resume(void) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    socket_start_read(sock);
    return self;
}

// Socket.pause() — stop libuv reads until the socket is destroyed/resumed.
static Item js_socket_pause(void) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (sock && !sock->destroyed) {
        int r = uv_read_stop((uv_stream_t*)&sock->tcp);
        if (r == 0) sock->reading = false;
    }
    return self;
}

// Socket.address() — return local address info
static Item js_socket_address(void) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (!sock) return ItemNull;

    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    int r = uv_tcp_getsockname(&sock->tcp, (struct sockaddr*)&addr, &addrlen);
    if (r != 0) return ItemNull;

    Item result = js_new_object();
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* a4 = (struct sockaddr_in*)&addr;
        char ip[64];
        uv_ip4_name(a4, ip, sizeof(ip));
        js_property_set(result, make_string_item("address"), make_string_item(ip));
        js_property_set(result, make_string_item("family"), make_string_item("IPv4"));
        js_property_set(result, make_string_item("port"), (Item){.item = i2it(ntohs(a4->sin_port))});
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*)&addr;
        char ip[128];
        uv_ip6_name(a6, ip, sizeof(ip));
        js_property_set(result, make_string_item("address"), make_string_item(ip));
        js_property_set(result, make_string_item("family"), make_string_item("IPv6"));
        js_property_set(result, make_string_item("port"), (Item){.item = i2it(ntohs(a6->sin6_port))});
    }
    return result;
}

// create a JS socket object wrapping a JsSocket
static Item make_socket_object(JsSocket* sock) {
    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_SOCKET);  // A3-T3b
    js_property_set(obj, make_string_item("__handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)sock)});
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_socket_on, 2));
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_socket_write, 2));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_socket_end, 1));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_socket_destroy, 1));
    // Additional Socket properties
    js_property_set(obj, make_string_item("readable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("writable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = ITEM_FALSE});
    js_property_set(obj, make_string_item("bytesRead"), (Item){.item = i2it(0)});
    js_property_set(obj, make_string_item("bytesWritten"), (Item){.item = i2it(0)});
    js_property_set(obj, make_string_item("bufferSize"), (Item){.item = i2it(0)});
    js_property_set(obj, make_string_item("_handle"), ItemNull);
    js_property_set(obj, make_string_item("allowHalfOpen"), (Item){.item = ITEM_FALSE});
    // Additional Socket methods
    js_property_set(obj, make_string_item("setTimeout"),
                    js_new_function((void*)js_socket_setTimeout, 2));
    js_property_set(obj, make_string_item("connect"),
                    js_new_function((void*)js_socket_connect, -1));
    js_property_set(obj, make_string_item("setKeepAlive"),
                    js_new_function((void*)js_socket_setKeepAlive, 2));
    js_property_set(obj, make_string_item("setNoDelay"),
                    js_new_function((void*)js_socket_setNoDelay, 1));
    js_property_set(obj, make_string_item("setEncoding"),
                    js_new_function((void*)js_socket_setEncoding, 1));
    js_property_set(obj, make_string_item("pipe"),
                    js_new_function((void*)js_socket_pipe, 1));
    js_property_set(obj, make_string_item("ref"),
                    js_new_function((void*)js_socket_ref, 0));
    js_property_set(obj, make_string_item("unref"),
                    js_new_function((void*)js_socket_unref, 0));
    js_property_set(obj, make_string_item("cork"),
                    js_new_function((void*)js_socket_cork, 0));
    js_property_set(obj, make_string_item("uncork"),
                    js_new_function((void*)js_socket_uncork, 0));
    js_property_set(obj, make_string_item("resume"),
                    js_new_function((void*)js_socket_resume, 0));
    js_property_set(obj, make_string_item("pause"),
                    js_new_function((void*)js_socket_pause, 0));
    js_property_set(obj, make_string_item("address"),
                    js_new_function((void*)js_socket_address, 0));
    sock->js_object = obj;
    return obj;
}

// =============================================================================
// net.createConnection(port, host) — TCP client
// =============================================================================

typedef struct NetConnectOptions {
    int port;
    int family;
    char host[256];
    Item callback;
} NetConnectOptions;

typedef struct NetResolveReq {
    uv_getaddrinfo_t req;
    JsSocket* sock;
    int port;
    int family;
    char host[256];
    char service[16];
} NetResolveReq;

static void client_connect_cb(uv_connect_t* req, int status);

static Item make_uv_error(int status, const char* syscall, const char* host, int port) {
    const char* code = uv_err_name(status);
    if (strcmp(syscall, "getaddrinfo") == 0 && strcmp(code, "EAI_NONAME") == 0) {
        code = "ENOTFOUND";
    }
    char msg[512];
    if (strcmp(syscall, "getaddrinfo") == 0) {
        snprintf(msg, sizeof(msg), "%s %s %s", syscall, code, host ? host : "");
    } else if (host && port >= 0) {
        snprintf(msg, sizeof(msg), "%s %s %s:%d", syscall, code, host, port);
    } else {
        snprintf(msg, sizeof(msg), "%s %s", syscall, code);
    }

    Item err = js_new_error(make_string_item(msg));
    js_property_set(err, make_string_item("code"), make_string_item(code));
    js_property_set(err, make_string_item("errno"), (Item){.item = i2it(status)});
    js_property_set(err, make_string_item("syscall"), make_string_item(syscall));
    if (host) js_property_set(err, make_string_item("address"), make_string_item(host));
    if (port >= 0) js_property_set(err, make_string_item("port"), (Item){.item = i2it(port)});
    return err;
}

static Item throw_missing_connect_args(void) {
    return js_throw_error_with_code(
        "ERR_MISSING_ARGS",
        "The \"options\" or \"port\" or \"path\" argument must be specified");
}

static Item throw_bad_port(Item value) {
    char msg[256];
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(value);
        int len = (int)s->len < 80 ? (int)s->len : 80;
        char buf[96];
        memcpy(buf, s->chars, (size_t)len);
        buf[len] = '\0';
        snprintf(msg, sizeof(msg), "Port should be >= 0 and < 65536. Received %s", buf);
    } else if (type == LMD_TYPE_INT) {
        snprintf(msg, sizeof(msg), "Port should be >= 0 and < 65536. Received %lld",
            (long long)it2i(value));
    } else if (type == LMD_TYPE_FLOAT) {
        snprintf(msg, sizeof(msg), "Port should be >= 0 and < 65536. Received %g", it2d(value));
    } else {
        snprintf(msg, sizeof(msg), "Port should be >= 0 and < 65536");
    }
    return js_throw_range_error_code("ERR_SOCKET_BAD_PORT", msg);
}

static bool parse_port(Item value, int* out_port) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        int64_t p = it2i(value);
        if (p < 0 || p > 65535) {
            throw_bad_port(value);
            return false;
        }
        *out_port = (int)p;
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        if (d != d || d == 1.0 / 0.0 || d == -1.0 / 0.0 || d < 0 || d > 65535 || d != (int64_t)d) {
            throw_bad_port(value);
            return false;
        }
        *out_port = (int)d;
        return true;
    }
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (s->len == 0 || s->len >= 64) {
            throw_bad_port(value);
            return false;
        }
        char buf[64];
        memcpy(buf, s->chars, s->len);
        buf[s->len] = '\0';
        char* end = NULL;
        long p = strtol(buf, &end, 0);
        if (end == buf || *end != '\0' || p < 0 || p > 65535) {
            throw_bad_port(value);
            return false;
        }
        *out_port = (int)p;
        return true;
    }
    js_throw_invalid_arg_type("port", "number or string", value);
    return false;
}

static bool copy_string_item(Item value, char* out, int out_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return false;
    String* s = it2s(value);
    int len = (int)s->len < out_size - 1 ? (int)s->len : out_size - 1;
    memcpy(out, s->chars, (size_t)len);
    out[len] = '\0';
    return true;
}

static bool option_is_true(Item options, const char* name) {
    Item value = js_property_get(options, make_string_item(name));
    return get_type_id(value) == LMD_TYPE_BOOL && it2b(value);
}

static bool validate_unsupported_stream_options(Item options) {
    if (option_is_true(options, "objectMode")) {
        js_throw_type_error_code(
            "ERR_INVALID_ARG_VALUE",
            "The property 'options.objectMode' is not supported. Received true");
        return false;
    }
    if (option_is_true(options, "readableObjectMode")) {
        js_throw_type_error_code(
            "ERR_INVALID_ARG_VALUE",
            "The property 'options.readableObjectMode' is not supported. Received true");
        return false;
    }
    if (option_is_true(options, "writableObjectMode")) {
        js_throw_type_error_code(
            "ERR_INVALID_ARG_VALUE",
            "The property 'options.writableObjectMode' is not supported. Received true");
        return false;
    }
    return true;
}

static bool normalize_options_object(Item options, NetConnectOptions* out) {
    if (!validate_unsupported_stream_options(options)) return false;

    Item hints = js_property_get(options, make_string_item("hints"));
    if (!is_undefined_item(hints) && hints.item != ITEM_NULL) {
        if (get_type_id(hints) != LMD_TYPE_INT || it2i(hints) != 0) {
            char msg[128];
            int64_t h = get_type_id(hints) == LMD_TYPE_INT ? it2i(hints) : 0;
            snprintf(msg, sizeof(msg), "The argument 'hints' is invalid. Received %lld", (long long)h);
            js_throw_type_error_code("ERR_INVALID_ARG_VALUE", msg);
            return false;
        }
    }

    Item port = js_property_get(options, make_string_item("port"));
    Item path = js_property_get(options, make_string_item("path"));
    if (is_undefined_item(port) && (is_undefined_item(path) || path.item == ITEM_NULL)) {
        throw_missing_connect_args();
        return false;
    }
    if (!parse_port(port, &out->port)) return false;

    Item host = js_property_get(options, make_string_item("host"));
    if (!is_undefined_item(host) && host.item != ITEM_NULL) {
        if (!copy_string_item(host, out->host, (int)sizeof(out->host))) {
            js_throw_invalid_arg_type("options.host", "string", host);
            return false;
        }
    }

    Item family = js_property_get(options, make_string_item("family"));
    if (!is_undefined_item(family) && family.item != ITEM_NULL && get_type_id(family) == LMD_TYPE_INT) {
        out->family = (int)it2i(family);
    }
    return true;
}

static bool normalize_connect_args(Item rest_args, NetConnectOptions* out) {
    out->port = 0;
    out->family = 0;
    out->host[0] = '\0';
    memcpy(out->host, "127.0.0.1", 10);
    out->callback = make_undefined_item();

    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 16 ? 16 : (int)argc64;
    if (argc <= 0) {
        throw_missing_connect_args();
        return false;
    }

    Item first = js_array_get_int(rest_args, 0);
    if (is_undefined_item(first)) {
        throw_missing_connect_args();
        return false;
    }

    if (get_type_id(first) == LMD_TYPE_MAP) {
        if (!normalize_options_object(first, out)) return false;
        if (argc > 1) {
            Item cb = js_array_get_int(rest_args, 1);
            if (is_callable(cb)) out->callback = cb;
        }
        return true;
    }

    if (!parse_port(first, &out->port)) return false;

    if (argc > 1) {
        Item second = js_array_get_int(rest_args, 1);
        if (is_callable(second)) {
            out->callback = second;
            return true;
        }
        if (!is_undefined_item(second) && second.item != ITEM_NULL) {
            if (!copy_string_item(second, out->host, (int)sizeof(out->host))) {
                js_throw_invalid_arg_type("host", "string", second);
                return false;
            }
        }
    }
    if (argc > 2) {
        Item third = js_array_get_int(rest_args, 2);
        if (is_callable(third)) out->callback = third;
    }
    return true;
}

static void client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSocket* sock = (JsSocket*)stream->data;
    if (nread > 0 && sock) {
        sock->bytes_read += (int64_t)nread;
        socket_update_io_counters(sock);
        Item data = make_string_item(buf->base, (int)nread);
        socket_emit(sock->js_object, "data", &data, 1);
        socket_pipe_data(sock->js_object, data);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0) {
        if (sock) {
            sock->reading = false;
            if (nread != UV_EOF) {
                Item err = make_uv_error((int)nread, "read", NULL, -1);
                socket_emit(sock->js_object, "error", &err, 1);
                if (sock->destroyed) return;
            }
            socket_emit(sock->js_object, "end", NULL, 0);
            socket_pipe_end(sock->js_object);
            socket_finish_on_remote_end(sock);
            if (!sock->destroyed) {
                sock->destroyed = true;
                uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
                    JsSocket* s = (JsSocket*)h->data;
                    if (s) {
                        socket_emit(s->js_object, "close", NULL, 0);
                        mem_free(s);
                    }
                });
            }
        }
    }
}

static int socket_connect_resolved(JsSocket* sock, const struct sockaddr* addr) {
    if (!sock || sock->destroyed) return 0;
    uv_connect_t* creq = (uv_connect_t*)mem_calloc(1, sizeof(uv_connect_t), MEM_CAT_JS_RUNTIME);
    creq->data = sock;
    sock->connect_pending = true;
    int r = uv_tcp_connect(creq, &sock->tcp, addr, client_connect_cb);
    if (r != 0) {
        sock->connect_pending = false;
        mem_free(creq);
    }
    return r;
}

static void net_resolve_cb(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
    NetResolveReq* nr = (NetResolveReq*)req->data;
    if (!nr) {
        if (res) uv_freeaddrinfo(res);
        return;
    }

    JsSocket* sock = nr->sock;
    if (!sock) {
        if (res) uv_freeaddrinfo(res);
        mem_free(nr);
        return;
    }

    if (sock->destroyed) {
        sock->connect_pending = false;
        if (res) uv_freeaddrinfo(res);
        socket_close_now(sock);
        mem_free(nr);
        return;
    }

    if (status != 0 || !res) {
        sock->connect_pending = false;
        Item err = make_uv_error(status, "getaddrinfo", nr->host, -1);
        Item lookup_args[3] = { err, make_undefined_item(), make_undefined_item() };
        socket_emit(sock->js_object, "lookup", lookup_args, 3);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
        if (res) uv_freeaddrinfo(res);
        mem_free(nr);
        return;
    }

    char addr_str[INET6_ADDRSTRLEN];
    int family = 0;
    addr_str[0] = '\0';
    if (res->ai_family == AF_INET) {
        struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
        uv_ip4_name(sa, addr_str, sizeof(addr_str));
        family = 4;
    } else if (res->ai_family == AF_INET6) {
        struct sockaddr_in6* sa = (struct sockaddr_in6*)res->ai_addr;
        uv_ip6_name(sa, addr_str, sizeof(addr_str));
        family = 6;
    }
    if (addr_str[0] != '\0') {
        Item lookup_args[3] = {
            ItemNull,
            make_string_item(addr_str),
            (Item){.item = i2it(family)}
        };
        socket_emit(sock->js_object, "lookup", lookup_args, 3);
    }

    int r = socket_connect_resolved(sock, res->ai_addr);
    if (r != 0) {
        Item err = make_uv_error(r, "connect", nr->host, nr->port);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
    }

    uv_freeaddrinfo(res);
    mem_free(nr);
}

static int socket_start_connect(JsSocket* sock, const NetConnectOptions* options) {
    if (!sock || sock->destroyed) return 0;

    struct sockaddr_in addr4;
    if ((options->family == 0 || options->family == 4) &&
        uv_ip4_addr(options->host, options->port, &addr4) == 0) {
        return socket_connect_resolved(sock, (const struct sockaddr*)&addr4);
    }

    struct sockaddr_in6 addr6;
    if ((options->family == 0 || options->family == 6) &&
        uv_ip6_addr(options->host, options->port, &addr6) == 0) {
        return socket_connect_resolved(sock, (const struct sockaddr*)&addr6);
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return UV_EINVAL;

    NetResolveReq* nr = (NetResolveReq*)mem_calloc(1, sizeof(NetResolveReq), MEM_CAT_JS_RUNTIME);
    nr->sock = sock;
    nr->port = options->port;
    nr->family = options->family;
    memcpy(nr->host, options->host, sizeof(nr->host));
    snprintf(nr->service, sizeof(nr->service), "%d", options->port);
    nr->req.data = nr;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = options->family == 4 ? AF_INET : (options->family == 6 ? AF_INET6 : AF_UNSPEC);

    sock->connect_pending = true;
    int r = uv_getaddrinfo(loop, &nr->req, net_resolve_cb, nr->host, nr->service, &hints);
    if (r != 0) {
        sock->connect_pending = false;
        mem_free(nr);
    }
    return r;
}

static void client_connect_cb(uv_connect_t* req, int status) {
    JsSocket* sock = (JsSocket*)req->data;
    mem_free(req);

    if (!sock) return;
    sock->connect_pending = false;
    if (sock->destroyed) {
        socket_close_now(sock);
        return;
    }

    if (status != 0) {
        Item err = make_uv_error(status, "connect", NULL, -1);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
        return;
    }

    sock->connected = true;
    socket_emit(sock->js_object, "connect", NULL, 0);

    // start reading
    socket_start_read(sock);
}

static Item create_socket_for_connect(const NetConnectOptions* options) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("net: createConnection: no event loop");
        return ItemNull;
    }

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    Item obj = make_socket_object(sock);
    if (is_callable(options->callback)) socket_set_listener(obj, "connect", options->callback);

    int r = socket_start_connect(sock, options);
    if (r != 0) {
        Item err = make_uv_error(r, "connect", options->host, options->port);
        socket_emit(obj, "error", &err, 1);
        socket_close_now(sock);
    }
    return obj;
}

static Item js_socket_connect_args(Item self, Item rest_args) {
    NetConnectOptions options;
    if (!normalize_connect_args(rest_args, &options)) return ItemNull;

    JsSocket* sock = socket_from_object(self);
    if (!sock || sock->destroyed) return self;
    if (is_callable(options.callback)) socket_set_listener(self, "connect", options.callback);

    int r = socket_start_connect(sock, &options);
    if (r != 0) {
        Item err = make_uv_error(r, "connect", options.host, options.port);
        socket_emit(self, "error", &err, 1);
        socket_close_now(sock);
    }
    return self;
}

extern "C" Item js_net_createConnection(Item rest_args) {
    NetConnectOptions options;
    if (!normalize_connect_args(rest_args, &options)) return ItemNull;
    return create_socket_for_connect(&options);
}

// =============================================================================
// net.createServer(connectionHandler) — TCP server
// =============================================================================

typedef struct JsServer {
    uv_tcp_t tcp;
    Item     js_object;
    Item     connection_handler;
    bool     closed;
    bool     listen_pending;
} JsServer;

static void server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSocket* sock = (JsSocket*)stream->data;
    if (nread > 0 && sock) {
        sock->bytes_read += (int64_t)nread;
        socket_update_io_counters(sock);
        Item data = make_string_item(buf->base, (int)nread);
        socket_emit(sock->js_object, "data", &data, 1);
        socket_pipe_data(sock->js_object, data);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0 && sock && !sock->destroyed) {
        sock->reading = false;
        if (nread != UV_EOF) {
            Item err = make_uv_error((int)nread, "read", NULL, -1);
            socket_emit(sock->js_object, "error", &err, 1);
            if (sock->destroyed) return;
        }
        socket_emit(sock->js_object, "end", NULL, 0);
        socket_pipe_end(sock->js_object);
        socket_finish_on_remote_end(sock);
        sock->destroyed = true;
        uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
            JsSocket* s = (JsSocket*)h->data;
            if (s) {
                socket_emit(s->js_object, "close", NULL, 0);
                mem_free(s);
            }
        });
    }
}

static void server_connection_cb(uv_stream_t* server, int status) {
    if (status < 0) return;

    JsServer* srv = (JsServer*)server->data;
    if (!srv) return;

    uv_loop_t* loop = server->loop;

    JsSocket* client = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &client->tcp);
    client->tcp.data = client;

    if (uv_accept(server, (uv_stream_t*)&client->tcp) == 0) {
        Item client_obj = make_socket_object(client);
        client->connected = true;
        client->is_server_side = true;

        // call connection handler
        if (get_type_id(srv->connection_handler) == LMD_TYPE_FUNC) {
            js_call_function(srv->connection_handler, srv->js_object, &client_obj, 1);
            js_microtask_flush();
        }

        // emit 'connection' event
        Item on_conn = js_property_get(srv->js_object, make_string_item("__on_connection__"));
        if (get_type_id(on_conn) == LMD_TYPE_FUNC) {
            js_call_function(on_conn, srv->js_object, &client_obj, 1);
        }

        socket_start_read(client);
    } else {
        uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
            mem_free(h->data);
        });
    }
}

static Item js_server_emit_listening_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();

    Item self = env[0];
    Item callback = env[1];
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL || is_undefined_item(handle_item)) {
        return make_undefined_item();
    }

    JsServer* srv = (JsServer*)(uintptr_t)it2i(handle_item);
    if (!srv || srv->closed || !srv->listen_pending) return make_undefined_item();

    srv->listen_pending = false;
    Item on_listening = js_property_get(self, make_string_item("__on_listening__"));
    if (is_callable(on_listening)) {
        js_call_function(on_listening, self, NULL, 0);
    }
    if (is_callable(callback)) {
        js_call_function(callback, self, NULL, 0);
    }
    js_microtask_flush();
    return make_undefined_item();
}

static void server_schedule_listening(Item self, JsServer* srv, Item callback) {
    if (!srv || srv->closed) return;
    srv->listen_pending = true;

    Item* env = js_alloc_env(2);
    env[0] = self;
    env[1] = callback;
    Item fn = js_new_closure((void*)js_server_emit_listening_scheduled, 0, env, 2);
    js_next_tick_enqueue(fn);
}

// server.listen(port, [host], [callback])
extern "C" Item js_server_listen(Item port_item, Item host_item, Item callback) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsServer* srv = (JsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    if (is_callable(host_item)) {
        callback = host_item;
        host_item = make_undefined_item();
    }

    int port = (int)it2i(port_item);
    char host_buf[256] = "0.0.0.0";
    if (get_type_id(host_item) == LMD_TYPE_STRING) {
        String* h = it2s(host_item);
        int len = (int)h->len < 255 ? (int)h->len : 255;
        memcpy(host_buf, h->chars, (size_t)len);
        host_buf[len] = '\0';
    }

    struct sockaddr_in addr;
    uv_ip4_addr(host_buf, port, &addr);

    uv_tcp_bind(&srv->tcp, (const struct sockaddr*)&addr, 0);
    int r = uv_listen((uv_stream_t*)&srv->tcp, 128, server_connection_cb);
    if (r != 0) {
        log_error("net: listen: failed: %s", uv_strerror(r));
        return self;
    }

    server_schedule_listening(self, srv, callback);
    return self;
}

// server.address() — returns {address, family, port} of the listening socket
static Item js_server_address(void) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL) return ItemNull;
    JsServer* srv = (JsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return ItemNull;

    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    int r = uv_tcp_getsockname(&srv->tcp, (struct sockaddr*)&addr, &addrlen);
    if (r != 0) return ItemNull;

    Item result = js_new_object();
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in* a4 = (struct sockaddr_in*)&addr;
        char ip[64];
        uv_ip4_name(a4, ip, sizeof(ip));
        js_property_set(result, make_string_item("address"), make_string_item(ip));
        js_property_set(result, make_string_item("family"), make_string_item("IPv4"));
        js_property_set(result, make_string_item("port"), (Item){.item = i2it(ntohs(a4->sin_port))});
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*)&addr;
        char ip[128];
        uv_ip6_name(a6, ip, sizeof(ip));
        js_property_set(result, make_string_item("address"), make_string_item(ip));
        js_property_set(result, make_string_item("family"), make_string_item("IPv6"));
        js_property_set(result, make_string_item("port"), (Item){.item = i2it(ntohs(a6->sin6_port))});
    }
    return result;
}

// server.ref() / server.unref() — stubs
static Item js_server_ref(void) { return js_get_this(); }
static Item js_server_unref(void) { return js_get_this(); }

// server.getConnections(callback) — stub: always reports 0
static Item js_server_getConnections(Item callback) {
    Item self = js_get_this();
    if (is_callable(callback)) {
        Item args[2] = { ItemNull, (Item){.item = i2it(0)} };
        js_call_function(callback, self, args, 2);
    }
    return self;
}

// server.close()
extern "C" Item js_server_close(Item callback) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsServer* srv = (JsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;
    srv->closed = true;
    srv->listen_pending = false;
    if (is_callable(callback)) {
        js_property_set(self, make_string_item("__on_close__"), callback);
    }

    if (!uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_close((uv_handle_t*)&srv->tcp, [](uv_handle_t* h) {
            JsServer* s = (JsServer*)h->data;
            if (s) {
                Item on_close = js_property_get(s->js_object, make_string_item("__on_close__"));
                if (is_callable(on_close)) {
                    js_call_function(on_close, s->js_object, NULL, 0);
                }
                mem_free(s);
            }
        });
    }
    return self;
}

// server.on(event, callback)
extern "C" Item js_server_on(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    return self;
}

extern "C" Item js_net_createServer(Item handler) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("net: createServer: no event loop");
        return ItemNull;
    }

    JsServer* srv = (JsServer*)mem_calloc(1, sizeof(JsServer), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &srv->tcp);
    srv->tcp.data = srv;
    srv->connection_handler = handler;

    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_SERVER);  // A3-T3b
    js_property_set(obj, make_string_item("__server__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)srv)});
    js_property_set(obj, make_string_item("listen"),
                    js_new_function((void*)js_server_listen, 3));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_server_close, 1));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_server_on, 2));
    js_property_set(obj, make_string_item("address"),
                    js_new_function((void*)js_server_address, 0));
    js_property_set(obj, make_string_item("ref"),
                    js_new_function((void*)js_server_ref, 0));
    js_property_set(obj, make_string_item("unref"),
                    js_new_function((void*)js_server_unref, 0));
    js_property_set(obj, make_string_item("getConnections"),
                    js_new_function((void*)js_server_getConnections, 1));

    srv->js_object = obj;
    return obj;
}

// =============================================================================
// net.isIP(input) — returns 0, 4, or 6
// =============================================================================

extern "C" Item js_net_isIP(Item input_item) {
    // Coerce to string (Node.js calls toString() on the input)
    if (get_type_id(input_item) != LMD_TYPE_STRING) {
        if (get_type_id(input_item) == LMD_TYPE_MAP || get_type_id(input_item) == LMD_TYPE_ELEMENT) {
            input_item = js_to_string(input_item);
            if (get_type_id(input_item) != LMD_TYPE_STRING) return (Item){.item = i2it(0)};
        } else {
            return (Item){.item = i2it(0)};
        }
    }
    String* s = it2s(input_item);
    char buf[256];
    int len = (int)s->len < 255 ? (int)s->len : 255;
    memcpy(buf, s->chars, (size_t)len);
    buf[len] = '\0';

    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    if (uv_ip4_addr(buf, 0, &addr4) == 0) return (Item){.item = i2it(4)};
    // Reject zone IDs with invalid characters (Node.js is stricter than libuv)
    char* pct = strchr(buf, '%');
    if (pct) {
        for (char* p = pct + 1; *p; p++) {
            if (*p == '@' || *p == '[' || *p == ']' || *p == '/') return (Item){.item = i2it(0)};
        }
    }
    if (uv_ip6_addr(buf, 0, &addr6) == 0) return (Item){.item = i2it(6)};
    return (Item){.item = i2it(0)};
}

extern "C" Item js_net_isIPv4(Item input) {
    Item r = js_net_isIP(input);
    return (Item){.item = b2it(it2i(r) == 4)};
}

extern "C" Item js_net_isIPv6(Item input) {
    Item r = js_net_isIP(input);
    return (Item){.item = b2it(it2i(r) == 6)};
}

// =============================================================================
// net.Socket() constructor alias
// =============================================================================

extern "C" Item js_net_Socket(void) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return ItemNull;

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    return make_socket_object(sock);
}

// =============================================================================
// net Module Namespace
// =============================================================================

static Item net_namespace = {0};
static int net_auto_select_family_timeout = 250; // Node.js default

static void net_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

static Item js_net_getDefaultAutoSelectFamilyAttemptTimeout(void) {
    return (Item){.item = i2it(net_auto_select_family_timeout)};
}

static Item js_net_setDefaultAutoSelectFamilyAttemptTimeout(Item timeout_item) {
    if (get_type_id(timeout_item) == LMD_TYPE_INT)
        net_auto_select_family_timeout = (int)it2i(timeout_item);
    return (Item){.item = ITEM_UNDEFINED};
}

extern "C" Item js_get_net_namespace(void) {
    if (net_namespace.item != 0) return net_namespace;

    net_namespace = js_new_object();

    net_set_method(net_namespace, "createServer",     (void*)js_net_createServer, 1);
    net_set_method(net_namespace, "createConnection", (void*)js_net_createConnection, -1);
    net_set_method(net_namespace, "connect",          (void*)js_net_createConnection, -1); // alias
    net_set_method(net_namespace, "Socket",           (void*)js_net_Socket, 0);
    net_set_method(net_namespace, "Server",           (void*)js_net_createServer, 1); // alias
    net_set_method(net_namespace, "isIP",             (void*)js_net_isIP, 1);
    net_set_method(net_namespace, "isIPv4",           (void*)js_net_isIPv4, 1);
    net_set_method(net_namespace, "isIPv6",           (void*)js_net_isIPv6, 1);
    net_set_method(net_namespace, "getDefaultAutoSelectFamilyAttemptTimeout",
                   (void*)js_net_getDefaultAutoSelectFamilyAttemptTimeout, 0);
    net_set_method(net_namespace, "setDefaultAutoSelectFamilyAttemptTimeout",
                   (void*)js_net_setDefaultAutoSelectFamilyAttemptTimeout, 1);

    Item default_key = make_string_item("default");
    js_property_set(net_namespace, default_key, net_namespace);

    return net_namespace;
}

extern "C" void js_net_reset(void) {
    net_namespace = (Item){0};
}
