/**
 * js_net.cpp — Node.js-style 'net' module for LambdaJS
 *
 * Provides net.createServer() and net.createConnection() backed by libuv TCP.
 * Registered as built-in module 'net' via js_module_get().
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

extern Input* js_input;

static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)(len > 0 ? len : 0));
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// =============================================================================
// Socket — wraps uv_tcp_t with event model
// =============================================================================

typedef struct JsSocket {
    uv_tcp_t tcp;
    Item      js_object;     // the JS object representing this socket
    bool      connected;
    bool      destroyed;
} JsSocket;

// emit event on socket JS object
static void socket_emit(Item obj, const char* event, Item* args, int argc) {
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item cb = js_property_get(obj, make_string_item(key));
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        js_call_function(cb, obj, args, argc);
        js_microtask_flush();
    }
}

// on(event, callback) — store as __on_<event>__
extern "C" Item js_socket_on(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    return self;
}

// write(data) — write to socket
extern "C" Item js_socket_write(Item self, Item data_item) {
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0) return (Item){.item = b2it(false)};
    JsSocket* sock = (JsSocket*)(uintptr_t)it2i(handle_item);
    if (!sock || sock->destroyed) return (Item){.item = b2it(false)};

    const char* data = NULL;
    size_t data_len = 0;
    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        data = s->chars;
        data_len = s->len;
    } else {
        return (Item){.item = b2it(false)};
    }

    uv_buf_t buf = uv_buf_init((char*)data, (unsigned int)data_len);
    uv_write_t* req = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_JS_RUNTIME);
    // copy data since it may be GC'd
    char* copy = (char*)mem_alloc(data_len, MEM_CAT_JS_RUNTIME);
    memcpy(copy, data, data_len);
    buf = uv_buf_init(copy, (unsigned int)data_len);
    req->data = copy;

    int r = uv_write(req, (uv_stream_t*)&sock->tcp, &buf, 1,
        [](uv_write_t* req, int status) {
            if (req->data) mem_free(req->data);
            mem_free(req);
        });

    if (r != 0) {
        mem_free(copy);
        mem_free(req);
        return (Item){.item = b2it(false)};
    }

    return (Item){.item = b2it(true)};
}

// end() — half-close (shutdown write side)
extern "C" Item js_socket_end(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0) return self;
    JsSocket* sock = (JsSocket*)(uintptr_t)it2i(handle_item);
    if (!sock || sock->destroyed) return self;

    uv_shutdown_t* sreq = (uv_shutdown_t*)mem_calloc(1, sizeof(uv_shutdown_t), MEM_CAT_JS_RUNTIME);
    sreq->data = sock;
    uv_shutdown(sreq, (uv_stream_t*)&sock->tcp,
        [](uv_shutdown_t* req, int status) {
            mem_free(req);
        });

    return self;
}

// destroy() — close socket
extern "C" Item js_socket_destroy(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0) return self;
    JsSocket* sock = (JsSocket*)(uintptr_t)it2i(handle_item);
    if (!sock || sock->destroyed) return self;

    sock->destroyed = true;
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
            JsSocket* s = (JsSocket*)handle->data;
            if (s) {
                socket_emit(s->js_object, "close", NULL, 0);
                mem_free(s);
            }
        });
    }
    return self;
}

// Socket.setTimeout(msecs, callback) — set idle timeout
static Item js_socket_setTimeout(Item msecs, Item callback) {
    Item self = js_get_this();
    // Store timeout value; if callback provided, add as 'timeout' listener
    js_property_set(self, make_string_item("timeout"), msecs);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        // Register 'timeout' listener
        Item args[] = { make_string_item("timeout"), callback };
        Item on_fn = js_property_get(self, make_string_item("on"));
        if (get_type_id(on_fn) == LMD_TYPE_FUNC) {
            js_call_function(on_fn, self, args, 2);
        }
    }
    return self;
}

// Socket.connect(options/port, host, callback) — initiate connection
static Item js_socket_connect(Item port_or_options, Item host, Item callback) {
    Item self = js_get_this();
    // For now, just emit 'connect' event if callback provided
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[] = { make_string_item("connect"), callback };
        Item on_fn = js_property_get(self, make_string_item("on"));
        if (get_type_id(on_fn) == LMD_TYPE_FUNC) {
            js_call_function(on_fn, self, args, 2);
        }
    }
    return self;
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

// Socket.address() — return local address info
static Item js_socket_address(void) {
    Item result = js_new_object();
    js_property_set(result, make_string_item("address"), make_string_item("127.0.0.1"));
    js_property_set(result, make_string_item("family"), make_string_item("IPv4"));
    js_property_set(result, make_string_item("port"), (Item){.item = i2it(0)});
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
                    js_new_function((void*)js_socket_on, 3));
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_socket_write, 2));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_socket_end, 1));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_socket_destroy, 1));
    // Additional Socket properties
    js_property_set(obj, make_string_item("readable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("writable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("_handle"), ItemNull);
    js_property_set(obj, make_string_item("allowHalfOpen"), (Item){.item = ITEM_FALSE});
    // Additional Socket methods
    js_property_set(obj, make_string_item("setTimeout"),
                    js_new_function((void*)js_socket_setTimeout, 2));
    js_property_set(obj, make_string_item("connect"),
                    js_new_function((void*)js_socket_connect, 3));
    js_property_set(obj, make_string_item("setKeepAlive"),
                    js_new_function((void*)js_socket_setKeepAlive, 2));
    js_property_set(obj, make_string_item("setNoDelay"),
                    js_new_function((void*)js_socket_setNoDelay, 1));
    js_property_set(obj, make_string_item("ref"),
                    js_new_function((void*)js_socket_ref, 0));
    js_property_set(obj, make_string_item("unref"),
                    js_new_function((void*)js_socket_unref, 0));
    js_property_set(obj, make_string_item("address"),
                    js_new_function((void*)js_socket_address, 0));
    sock->js_object = obj;
    return obj;
}

// =============================================================================
// net.createConnection(port, host) — TCP client
// =============================================================================

static void client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSocket* sock = (JsSocket*)stream->data;
    if (nread > 0 && sock) {
        Item data = make_string_item(buf->base, (int)nread);
        socket_emit(sock->js_object, "data", &data, 1);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0) {
        if (sock) {
            socket_emit(sock->js_object, "end", NULL, 0);
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

static void client_connect_cb(uv_connect_t* req, int status) {
    JsSocket* sock = (JsSocket*)req->data;
    mem_free(req);

    if (status != 0) {
        if (sock) {
            Item err = js_new_error(make_string_item(uv_strerror(status)));
            socket_emit(sock->js_object, "error", &err, 1);
        }
        return;
    }

    sock->connected = true;
    socket_emit(sock->js_object, "connect", NULL, 0);

    // start reading
    uv_read_start((uv_stream_t*)&sock->tcp, client_alloc_cb, client_read_cb);
}

extern "C" Item js_net_createConnection(Item port_item, Item host_item) {
    int port = (int)it2i(port_item);

    char host_buf[256] = "127.0.0.1";
    if (get_type_id(host_item) == LMD_TYPE_STRING) {
        String* h = it2s(host_item);
        int len = (int)h->len < 255 ? (int)h->len : 255;
        memcpy(host_buf, h->chars, (size_t)len);
        host_buf[len] = '\0';
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("net: createConnection: no event loop");
        return ItemNull;
    }

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    Item obj = make_socket_object(sock);

    struct sockaddr_in addr;
    uv_ip4_addr(host_buf, port, &addr);

    uv_connect_t* creq = (uv_connect_t*)mem_calloc(1, sizeof(uv_connect_t), MEM_CAT_JS_RUNTIME);
    creq->data = sock;

    int r = uv_tcp_connect(creq, &sock->tcp, (const struct sockaddr*)&addr, client_connect_cb);
    if (r != 0) {
        log_error("net: createConnection: connect failed: %s", uv_strerror(r));
        mem_free(creq);
        mem_free(sock);
        return ItemNull;
    }

    return obj;
}

// =============================================================================
// net.createServer(connectionHandler) — TCP server
// =============================================================================

typedef struct JsServer {
    uv_tcp_t tcp;
    Item     js_object;
    Item     connection_handler;
} JsServer;

static void server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSocket* sock = (JsSocket*)stream->data;
    if (nread > 0 && sock) {
        Item data = make_string_item(buf->base, (int)nread);
        socket_emit(sock->js_object, "data", &data, 1);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0 && sock && !sock->destroyed) {
        socket_emit(sock->js_object, "end", NULL, 0);
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

        uv_read_start((uv_stream_t*)&client->tcp, server_alloc_cb, server_client_read_cb);
    } else {
        uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
            mem_free(h->data);
        });
    }
}

// server.listen(port, [host], [callback])
extern "C" Item js_server_listen(Item self, Item port_item, Item host_item, Item callback) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsServer* srv = (JsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

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

    // emit 'listening'
    Item on_listening = js_property_get(self, make_string_item("__on_listening__"));
    if (get_type_id(on_listening) == LMD_TYPE_FUNC) {
        js_call_function(on_listening, self, NULL, 0);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }

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
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[2] = { ItemNull, (Item){.item = i2it(0)} };
        js_call_function(callback, self, args, 2);
    }
    return self;
}

// server.close()
extern "C" Item js_server_close(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsServer* srv = (JsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    if (!uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_close((uv_handle_t*)&srv->tcp, [](uv_handle_t* h) {
            JsServer* s = (JsServer*)h->data;
            if (s) {
                Item on_close = js_property_get(s->js_object, make_string_item("__on_close__"));
                if (get_type_id(on_close) == LMD_TYPE_FUNC) {
                    js_call_function(on_close, s->js_object, NULL, 0);
                }
                mem_free(s);
            }
        });
    }
    return self;
}

// server.on(event, callback)
extern "C" Item js_server_on(Item self, Item event_item, Item callback) {
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
                    js_new_function((void*)js_server_listen, 4));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_server_close, 1));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_server_on, 3));
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
    net_set_method(net_namespace, "createConnection", (void*)js_net_createConnection, 2);
    net_set_method(net_namespace, "connect",          (void*)js_net_createConnection, 2); // alias
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
