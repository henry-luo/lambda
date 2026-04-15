/**
 * js_tls.cpp — Node.js-style 'tls' module for LambdaJS
 *
 * Provides tls.connect(), tls.createServer(), tls.createSecureContext(),
 * and TLSSocket wrapping mbedTLS via lambda/serve/tls_handler.
 *
 * Registered as built-in module 'tls' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"
#include "../serve/tls_handler.hpp"

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

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// helper: extract C string from Item into stack buffer
static const char* item_to_cstr(Item val, char* buf, int buf_size) {
    if (get_type_id(val) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(val);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
}

// =============================================================================
// TLS Socket — wraps net.Socket + TlsConnection
// =============================================================================

typedef struct JsTlsSocket {
    uv_tcp_t       tcp;
    TlsContext*    tls_ctx;        // shared context (owned by this if client-created)
    TlsConnection* tls_conn;       // per-connection TLS state
    Item           js_object;
    bool           connected;
    bool           destroyed;
    bool           is_server;       // server-side vs client-side
    bool           owns_context;    // whether we should free tls_ctx
} JsTlsSocket;

// emit event on TLS socket JS object
static void tls_socket_emit(Item obj, const char* event, Item* args, int argc) {
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item cb = js_property_get(obj, make_string_item(key));
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        js_call_function(cb, obj, args, argc);
        js_microtask_flush();
    }
}

// on(event, callback)
extern "C" Item js_tls_socket_on(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    return self;
}

// write(data)
extern "C" Item js_tls_socket_write(Item self, Item data_item) {
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0) return (Item){.item = b2it(false)};
    JsTlsSocket* sock = (JsTlsSocket*)(uintptr_t)it2i(handle_item);
    if (!sock || sock->destroyed || !sock->tls_conn) return (Item){.item = b2it(false)};

    const char* data = NULL;
    size_t data_len = 0;
    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        data = s->chars;
        data_len = s->len;
    } else {
        return (Item){.item = b2it(false)};
    }

    int written = tls_write(sock->tls_conn, (const unsigned char*)data, data_len);
    return (Item){.item = b2it(written >= 0)};
}

// end()
extern "C" Item js_tls_socket_end(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0) return self;
    JsTlsSocket* sock = (JsTlsSocket*)(uintptr_t)it2i(handle_item);
    if (!sock || sock->destroyed) return self;

    uv_shutdown_t* sreq = (uv_shutdown_t*)mem_calloc(1, sizeof(uv_shutdown_t), MEM_CAT_JS_RUNTIME);
    sreq->data = sock;
    uv_shutdown(sreq, (uv_stream_t*)&sock->tcp,
        [](uv_shutdown_t* req, int status) {
            mem_free(req);
        });
    return self;
}

// destroy()
extern "C" Item js_tls_socket_destroy(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0) return self;
    JsTlsSocket* sock = (JsTlsSocket*)(uintptr_t)it2i(handle_item);
    if (!sock || sock->destroyed) return self;

    sock->destroyed = true;
    if (sock->tls_conn) {
        tls_connection_destroy(sock->tls_conn);
        sock->tls_conn = NULL;
    }
    if (sock->owns_context && sock->tls_ctx) {
        tls_context_destroy(sock->tls_ctx);
        sock->tls_ctx = NULL;
    }
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
            JsTlsSocket* s = (JsTlsSocket*)handle->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                mem_free(s);
            }
        });
    }
    return self;
}

// getPeerCertificate() — returns object with subject
extern "C" Item js_tls_socket_getPeerCert(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0) return js_new_object();
    JsTlsSocket* sock = (JsTlsSocket*)(uintptr_t)it2i(handle_item);
    if (!sock || !sock->tls_conn) return js_new_object();

    Item cert = js_new_object();
    char* subject = tls_get_peer_subject(sock->tls_conn);
    if (subject) {
        js_property_set(cert, make_string_item("subject"), make_string_item(subject));
        mem_free(subject);
    }
    const char* cipher = tls_get_cipher_name(sock->tls_conn);
    if (cipher) {
        js_property_set(cert, make_string_item("cipher"), make_string_item(cipher));
    }
    const char* proto = tls_get_protocol_version(sock->tls_conn);
    if (proto) {
        js_property_set(cert, make_string_item("protocol"), make_string_item(proto));
    }
    return cert;
}

// authorized property
extern "C" Item js_tls_socket_getAuthorized(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0) return (Item){.item = b2it(false)};
    JsTlsSocket* sock = (JsTlsSocket*)(uintptr_t)it2i(handle_item);
    if (!sock || !sock->tls_conn) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(sock->tls_conn->handshake_done == 1)};
}

// create a JS TLSSocket object
static Item make_tls_socket_object(JsTlsSocket* sock) {
    Item obj = js_new_object();
    js_property_set(obj, make_string_item("__class_name__"), make_string_item("TLSSocket"));
    js_property_set(obj, make_string_item("__handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)sock)});
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_tls_socket_on, 3));
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_tls_socket_write, 2));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_tls_socket_end, 1));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_tls_socket_destroy, 1));
    js_property_set(obj, make_string_item("getPeerCertificate"),
                    js_new_function((void*)js_tls_socket_getPeerCert, 1));
    js_property_set(obj, make_string_item("encrypted"), (Item){.item = b2it(true)});
    sock->js_object = obj;
    return obj;
}

// =============================================================================
// tls.createSecureContext(options)
// =============================================================================

extern "C" Item js_tls_createSecureContext(Item options_item) {
    TlsConfig config = tls_config_default();

    char cert_buf[4096] = {0};
    char key_buf[4096] = {0};
    char ca_buf[4096] = {0};

    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        // extract cert, key, ca from options
        Item cert_item = js_property_get(options_item, make_string_item("cert"));
        Item key_item = js_property_get(options_item, make_string_item("key"));
        Item ca_item = js_property_get(options_item, make_string_item("ca"));

        if (get_type_id(cert_item) == LMD_TYPE_STRING) {
            item_to_cstr(cert_item, cert_buf, sizeof(cert_buf));
            config.cert_file = cert_buf;
        }
        if (get_type_id(key_item) == LMD_TYPE_STRING) {
            item_to_cstr(key_item, key_buf, sizeof(key_buf));
            config.key_file = key_buf;
        }
        if (get_type_id(ca_item) == LMD_TYPE_STRING) {
            item_to_cstr(ca_item, ca_buf, sizeof(ca_buf));
            config.ca_file = ca_buf;
        }
    }

    TlsContext* ctx = tls_context_create(&config);
    if (!ctx) {
        return js_new_error(make_string_item("Failed to create TLS context"));
    }

    Item result = js_new_object();
    js_property_set(result, make_string_item("__class_name__"), make_string_item("SecureContext"));
    js_property_set(result, make_string_item("__ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    return result;
}

// =============================================================================
// tls.connect(options[, callback]) — TLS client
// =============================================================================

static void tls_client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void tls_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsTlsSocket* sock = (JsTlsSocket*)stream->data;
    if (nread > 0 && sock && sock->tls_conn) {
        // data arrived on the TCP socket — read through TLS
        unsigned char tbuf[8192];
        int n = tls_read(sock->tls_conn, tbuf, sizeof(tbuf));
        if (n > 0) {
            Item data = make_string_item((const char*)tbuf, n);
            tls_socket_emit(sock->js_object, "data", &data, 1);
        }
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0 && sock && !sock->destroyed) {
        tls_socket_emit(sock->js_object, "end", NULL, 0);
        sock->destroyed = true;
        if (sock->tls_conn) {
            tls_connection_destroy(sock->tls_conn);
            sock->tls_conn = NULL;
        }
        uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
            JsTlsSocket* s = (JsTlsSocket*)h->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                if (s->owns_context && s->tls_ctx) tls_context_destroy(s->tls_ctx);
                mem_free(s);
            }
        });
    }
}

static void tls_client_connect_cb(uv_connect_t* req, int status) {
    JsTlsSocket* sock = (JsTlsSocket*)req->data;
    mem_free(req);

    if (status != 0) {
        if (sock) {
            Item err = js_new_error(make_string_item(uv_strerror(status)));
            tls_socket_emit(sock->js_object, "error", &err, 1);
        }
        return;
    }

    // TCP connected, now do TLS handshake
    sock->tls_conn = tls_connection_create(sock->tls_ctx, &sock->tcp);
    if (!sock->tls_conn) {
        Item err = js_new_error(make_string_item("TLS connection setup failed"));
        tls_socket_emit(sock->js_object, "error", &err, 1);
        return;
    }

    int hs = tls_handshake(sock->tls_conn);
    if (hs < 0) {
        Item err = js_new_error(make_string_item("TLS handshake failed"));
        tls_socket_emit(sock->js_object, "error", &err, 1);
        return;
    }

    sock->connected = true;
    js_property_set(sock->js_object, make_string_item("authorized"),
                    (Item){.item = b2it(true)});
    tls_socket_emit(sock->js_object, "secureConnect", NULL, 0);

    uv_read_start((uv_stream_t*)&sock->tcp, tls_client_alloc_cb, tls_client_read_cb);
}

extern "C" Item js_tls_connect(Item options_item) {
    int port = 443;
    char host_buf[256] = "127.0.0.1";

    // extract port, host from options
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item port_item = js_property_get(options_item, make_string_item("port"));
        if (get_type_id(port_item) == LMD_TYPE_INT) port = (int)it2i(port_item);
        Item host_item = js_property_get(options_item, make_string_item("host"));
        if (get_type_id(host_item) == LMD_TYPE_STRING) {
            item_to_cstr(host_item, host_buf, sizeof(host_buf));
        }
    } else if (get_type_id(options_item) == LMD_TYPE_INT) {
        port = (int)it2i(options_item);
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return js_new_error(make_string_item("No event loop available"));
    }

    // create TLS context (default config for client)
    TlsConfig config = tls_config_default();
    config.verify_peer = 0; // don't verify by default (like Node.js rejectUnauthorized: false)
    TlsContext* ctx = tls_context_create(&config);
    if (!ctx) {
        return js_new_error(make_string_item("Failed to create TLS context"));
    }

    JsTlsSocket* sock = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;
    sock->tls_ctx = ctx;
    sock->owns_context = true;
    sock->is_server = false;

    Item obj = make_tls_socket_object(sock);

    struct sockaddr_in addr;
    uv_ip4_addr(host_buf, port, &addr);

    uv_connect_t* creq = (uv_connect_t*)mem_calloc(1, sizeof(uv_connect_t), MEM_CAT_JS_RUNTIME);
    creq->data = sock;

    int r = uv_tcp_connect(creq, &sock->tcp, (const struct sockaddr*)&addr, tls_client_connect_cb);
    if (r != 0) {
        log_error("tls: connect: failed: %s", uv_strerror(r));
        mem_free(creq);
        tls_context_destroy(ctx);
        mem_free(sock);
        return js_new_error(make_string_item(uv_strerror(r)));
    }

    return obj;
}

// =============================================================================
// tls.createServer(options, connectionHandler) — TLS server
// =============================================================================

typedef struct JsTlsServer {
    uv_tcp_t     tcp;
    TlsContext*  tls_ctx;
    Item         js_object;
    Item         connection_handler;
} JsTlsServer;

static void tls_server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void tls_server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsTlsSocket* sock = (JsTlsSocket*)stream->data;
    if (nread > 0 && sock && sock->tls_conn) {
        unsigned char tbuf[8192];
        int n = tls_read(sock->tls_conn, tbuf, sizeof(tbuf));
        if (n > 0) {
            Item data = make_string_item((const char*)tbuf, n);
            tls_socket_emit(sock->js_object, "data", &data, 1);
        }
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0 && sock && !sock->destroyed) {
        tls_socket_emit(sock->js_object, "end", NULL, 0);
        sock->destroyed = true;
        if (sock->tls_conn) {
            tls_connection_destroy(sock->tls_conn);
            sock->tls_conn = NULL;
        }
        uv_close((uv_handle_t*)stream, [](uv_handle_t* h) {
            JsTlsSocket* s = (JsTlsSocket*)h->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                mem_free(s);
            }
        });
    }
}

static void tls_server_connection_cb(uv_stream_t* server, int status) {
    if (status < 0) return;
    JsTlsServer* srv = (JsTlsServer*)server->data;
    if (!srv) return;

    uv_loop_t* loop = server->loop;

    JsTlsSocket* client = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &client->tcp);
    client->tcp.data = client;
    client->tls_ctx = srv->tls_ctx;
    client->owns_context = false;
    client->is_server = true;

    if (uv_accept(server, (uv_stream_t*)&client->tcp) == 0) {
        // create TLS connection and do handshake
        client->tls_conn = tls_connection_create(srv->tls_ctx, &client->tcp);
        if (!client->tls_conn || tls_handshake(client->tls_conn) < 0) {
            log_error("tls: server handshake failed");
            if (client->tls_conn) tls_connection_destroy(client->tls_conn);
            uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
                mem_free(h->data);
            });
            return;
        }

        client->connected = true;
        Item client_obj = make_tls_socket_object(client);

        // call connection handler
        if (get_type_id(srv->connection_handler) == LMD_TYPE_FUNC) {
            js_call_function(srv->connection_handler, srv->js_object, &client_obj, 1);
            js_microtask_flush();
        }

        // emit 'secureConnection'
        tls_socket_emit(srv->js_object, "secureConnection", &client_obj, 1);

        uv_read_start((uv_stream_t*)&client->tcp, tls_server_alloc_cb, tls_server_client_read_cb);
    } else {
        uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
            mem_free(h->data);
        });
    }
}

// server.listen(port, [host], [callback])
extern "C" Item js_tls_server_listen(Item self, Item port_item, Item host_item, Item callback) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
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

    int r = uv_listen((uv_stream_t*)&srv->tcp, 128, tls_server_connection_cb);
    if (r != 0) {
        log_error("tls: server listen failed: %s", uv_strerror(r));
        return self;
    }

    Item on_listening = js_property_get(self, make_string_item("__on_listening__"));
    if (get_type_id(on_listening) == LMD_TYPE_FUNC) {
        js_call_function(on_listening, self, NULL, 0);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    return self;
}

// server.close()
extern "C" Item js_tls_server_close(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    if (!uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_close((uv_handle_t*)&srv->tcp, [](uv_handle_t* h) {
            JsTlsServer* s = (JsTlsServer*)h->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                if (s->tls_ctx) tls_context_destroy(s->tls_ctx);
                mem_free(s);
            }
        });
    }
    return self;
}

// server.on(event, callback)
extern "C" Item js_tls_server_on(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    return self;
}

extern "C" Item js_tls_createServer(Item options_item, Item handler) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return js_new_error(make_string_item("No event loop available"));
    }

    // extract cert/key from options
    TlsConfig config = tls_config_default();
    char cert_buf[4096] = {0};
    char key_buf[4096] = {0};

    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item cert_item = js_property_get(options_item, make_string_item("cert"));
        Item key_item = js_property_get(options_item, make_string_item("key"));
        if (get_type_id(cert_item) == LMD_TYPE_STRING) {
            item_to_cstr(cert_item, cert_buf, sizeof(cert_buf));
            config.cert_file = cert_buf;
        }
        if (get_type_id(key_item) == LMD_TYPE_STRING) {
            item_to_cstr(key_item, key_buf, sizeof(key_buf));
            config.key_file = key_buf;
        }
    }

    TlsContext* ctx = tls_context_create(&config);
    if (!ctx) {
        return js_new_error(make_string_item("Failed to create TLS context"));
    }

    JsTlsServer* srv = (JsTlsServer*)mem_calloc(1, sizeof(JsTlsServer), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &srv->tcp);
    srv->tcp.data = srv;
    srv->tls_ctx = ctx;
    srv->connection_handler = handler;

    Item obj = js_new_object();
    js_property_set(obj, make_string_item("__class_name__"), make_string_item("TLSServer"));
    js_property_set(obj, make_string_item("__server__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)srv)});
    js_property_set(obj, make_string_item("listen"),
                    js_new_function((void*)js_tls_server_listen, 4));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_tls_server_close, 1));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_tls_server_on, 3));

    srv->js_object = obj;
    return obj;
}

// =============================================================================
// tls Module Namespace
// =============================================================================

static Item tls_namespace = {0};

static void tls_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_tls_namespace(void) {
    if (tls_namespace.item != 0) return tls_namespace;

    tls_namespace = js_new_object();

    tls_set_method(tls_namespace, "connect",             (void*)js_tls_connect, 1);
    tls_set_method(tls_namespace, "createServer",        (void*)js_tls_createServer, 2);
    tls_set_method(tls_namespace, "createSecureContext",  (void*)js_tls_createSecureContext, 1);

    // TLS constants
    js_property_set(tls_namespace, make_string_item("DEFAULT_MIN_VERSION"),
                    make_string_item("TLSv1.2"));
    js_property_set(tls_namespace, make_string_item("DEFAULT_MAX_VERSION"),
                    make_string_item("TLSv1.3"));

    Item default_key = make_string_item("default");
    js_property_set(tls_namespace, default_key, tls_namespace);

    return tls_namespace;
}

extern "C" void js_tls_reset(void) {
    tls_namespace = (Item){0};
}
