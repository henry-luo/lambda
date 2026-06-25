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
#include "js_class.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"
#include "../serve/tls_handler.hpp"

#include <cstring>

extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" Item js_get_net_namespace(void);

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

static bool is_callable(Item item) {
    return get_type_id(item) == LMD_TYPE_FUNC;
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
    bool           tcp_initialized;
    bool           is_server;       // server-side vs client-side
    bool           owns_context;    // whether we should free tls_ctx
    bool           has_host;
    bool           has_port;
    bool           has_local_address;
    int            connect_port;
    char           connect_host[256];
    char           local_address[256];
} JsTlsSocket;

static JsTlsSocket* tls_socket_from_object(Item obj) {
    Item handle_item = js_property_get(obj, make_string_item("__handle__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (JsTlsSocket*)(uintptr_t)it2i(handle_item);
}

static void tls_socket_detach_js_object(Item obj) {
    js_property_set(obj, make_string_item("__handle__"), ItemNull);
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
}

// emit event on TLS socket JS object
static void tls_socket_emit(Item obj, const char* event, Item* args, int argc) {
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item listeners = js_property_get(obj, make_string_item(key));
    if (get_type_id(listeners) == LMD_TYPE_FUNC) {
        js_call_function(listeners, obj, args, argc);
        js_microtask_flush();
    } else if (get_type_id(listeners) == LMD_TYPE_ARRAY) {
        int64_t count = js_array_length(listeners);
        for (int64_t i = 0; i < count; i++) {
            Item cb = js_array_get_int(listeners, i);
            if (get_type_id(cb) == LMD_TYPE_FUNC) {
                js_call_function(cb, obj, args, argc);
            }
        }
        js_microtask_flush();
    }
}

static Item make_tls_econnreset_error(JsTlsSocket* sock) {
    Item err = js_new_error(make_string_item("Client network socket disconnected before secure TLS connection was established"));
    js_property_set(err, make_string_item("code"), make_string_item("ECONNRESET"));
    if (sock && sock->has_host) {
        js_property_set(err, make_string_item("host"), make_string_item(sock->connect_host));
    }
    if (sock && sock->has_port) {
        js_property_set(err, make_string_item("port"), (Item){.item = i2it(sock->connect_port)});
    }
    if (sock && sock->has_local_address) {
        js_property_set(err, make_string_item("localAddress"), make_string_item(sock->local_address));
    }
    return err;
}

// on(event, callback)
extern "C" Item js_tls_socket_on(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    Item key_item = make_string_item(key);
    Item existing = js_property_get(self, key_item);
    if (get_type_id(existing) == LMD_TYPE_FUNC) {
        Item arr = js_array_new(0);
        js_array_push(arr, existing);
        js_array_push(arr, callback);
        js_property_set(self, key_item, arr);
    } else if (get_type_id(existing) == LMD_TYPE_ARRAY) {
        js_array_push(existing, callback);
    } else {
        js_property_set(self, key_item, callback);
    }
    return self;
}

// once(event, callback)
extern "C" Item js_tls_socket_once(Item event_item, Item callback) {
    return js_tls_socket_on(event_item, callback);
}

// write(data)
extern "C" Item js_tls_socket_write(Item data_item) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
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
extern "C" Item js_tls_socket_end(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock || sock->destroyed) return self;
    if (!sock->tcp_initialized) {
        tls_socket_emit(self, "end", NULL, 0);
        tls_socket_emit(self, "close", NULL, 0);
        sock->destroyed = true;
        mem_free(sock);
        tls_socket_detach_js_object(self);
        return self;
    }

    uv_shutdown_t* sreq = (uv_shutdown_t*)mem_calloc(1, sizeof(uv_shutdown_t), MEM_CAT_JS_RUNTIME);
    sreq->data = sock;
    uv_shutdown(sreq, (uv_stream_t*)&sock->tcp,
        [](uv_shutdown_t* req, int status) {
            mem_free(req);
        });
    return self;
}

// destroy()
extern "C" Item js_tls_socket_destroy(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock) return self;
    if (sock->destroyed) {
        tls_socket_detach_js_object(self);
        return self;
    }

    sock->destroyed = true;
    tls_socket_detach_js_object(self);
    if (sock->tls_conn) {
        tls_connection_destroy(sock->tls_conn);
        sock->tls_conn = NULL;
    }
    if (sock->owns_context && sock->tls_ctx) {
        tls_context_destroy(sock->tls_ctx);
        sock->tls_ctx = NULL;
    }
    if (!sock->tcp_initialized) {
        tls_socket_emit(sock->js_object, "close", NULL, 0);
        mem_free(sock);
    } else if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
            JsTlsSocket* s = (JsTlsSocket*)handle->data;
            if (s) {
                tls_socket_emit(s->js_object, "close", NULL, 0);
                tls_socket_detach_js_object(s->js_object);
                mem_free(s);
            }
        });
    }
    return self;
}

// pipe(destination) — minimal stream compatibility for echo-style fixtures
extern "C" Item js_tls_socket_pipe(Item dest) {
    Item self = js_get_this();
    js_property_set(self, make_string_item("__pipe_dest__"), dest);
    return dest;
}

// getPeerCertificate() — returns object with subject
extern "C" Item js_tls_socket_getPeerCert(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
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
extern "C" Item js_tls_socket_getAuthorized(void) {
    Item self = js_get_this();
    JsTlsSocket* sock = tls_socket_from_object(self);
    if (!sock || !sock->tls_conn) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(sock->tls_conn->handshake_done == 1)};
}

// create a JS TLSSocket object
static Item make_tls_socket_object(JsTlsSocket* sock) {
    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_TLS_SOCKET);  // A3-T3b
    js_property_set(obj, make_string_item("__handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)sock)});
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_tls_socket_on, 2));
    js_property_set(obj, make_string_item("once"),
                    js_new_function((void*)js_tls_socket_once, 2));
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_tls_socket_write, 1));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_tls_socket_end, 0));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_tls_socket_destroy, 0));
    js_property_set(obj, make_string_item("pipe"),
                    js_new_function((void*)js_tls_socket_pipe, 1));
    js_property_set(obj, make_string_item("getPeerCertificate"),
                    js_new_function((void*)js_tls_socket_getPeerCert, 0));
    js_property_set(obj, make_string_item("encrypted"), (Item){.item = b2it(true)});
    js_property_set(obj, make_string_item("readable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("writable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = ITEM_FALSE});
    sock->js_object = obj;
    return obj;
}

// =============================================================================
// tls.createSecureContext(options)
// =============================================================================

extern "C" Item js_tls_createSecureContext(Item options_item) {
    TlsConfig config = tls_config_default();

    char cert_buf[16384] = {0};
    char key_buf[16384] = {0};
    char ca_buf[16384] = {0};

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
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(result, JS_CLASS_SECURE_CONTEXT);  // A3-T3b
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
                tls_socket_detach_js_object(s->js_object);
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
            if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
                uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
                    JsTlsSocket* s = (JsTlsSocket*)handle->data;
                    if (s) {
                        tls_socket_emit(s->js_object, "close", NULL, 0);
                        tls_socket_detach_js_object(s->js_object);
                        if (s->owns_context && s->tls_ctx) tls_context_destroy(s->tls_ctx);
                        mem_free(s);
                    }
                });
            }
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
        Item err = make_tls_econnreset_error(sock);
        tls_socket_emit(sock->js_object, "error", &err, 1);
        tls_socket_emit(sock->js_object, "close", NULL, 0);
        return;
    }

    sock->connected = true;
    js_property_set(sock->js_object, make_string_item("authorized"),
                    (Item){.item = b2it(true)});
    tls_socket_emit(sock->js_object, "secureConnect", NULL, 0);

    uv_read_start((uv_stream_t*)&sock->tcp, tls_client_alloc_cb, tls_client_read_cb);
}

static Item tls_emit_secure_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item obj = env[0];
    tls_socket_emit(obj, "secure", NULL, 0);
    return make_js_undefined();
}

static Item tls_emit_error_close_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    Item obj = env[0];
    Item err = env[1];
    if (!tls_socket_from_object(obj)) return make_js_undefined();
    tls_socket_emit(obj, "error", &err, 1);
    tls_socket_emit(obj, "close", NULL, 0);
    return make_js_undefined();
}

static void schedule_tls_secure_event(Item obj) {
    Item* env = js_alloc_env(1);
    env[0] = obj;
    Item callback = js_new_closure((void*)tls_emit_secure_later, 0, env, 1);
    js_next_tick_enqueue(callback);
}

static void schedule_tls_error_close(Item obj, Item err) {
    Item* env = js_alloc_env(2);
    env[0] = obj;
    env[1] = err;
    Item callback = js_new_closure((void*)tls_emit_error_close_later, 0, env, 2);
    js_setTimeout(callback, (Item){.item = i2it(0)});
}

extern "C" Item js_tls_TLSSocket(Item socket_item, Item options_item) {
    (void)socket_item;
    JsTlsSocket* sock = (JsTlsSocket*)mem_calloc(1, sizeof(JsTlsSocket), MEM_CAT_JS_RUNTIME);
    sock->is_server = false;
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item is_server = js_property_get(options_item, make_string_item("isServer"));
        sock->is_server = get_type_id(is_server) == LMD_TYPE_BOOL && it2b(is_server);
    }
    Item obj = make_tls_socket_object(sock);
    js_property_set(obj, make_string_item("authorized"), (Item){.item = b2it(false)});
    js_property_set(obj, make_string_item("alpnProtocol"), make_string_item("http/1.1"));
    if (sock->is_server) schedule_tls_secure_event(obj);
    return obj;
}

extern "C" Item js_tls_connect(Item options_item) {
    Item callback = make_js_undefined();
    Item rest_args = options_item;
    if (get_type_id(rest_args) == LMD_TYPE_ARRAY) {
        int64_t argc = js_array_length(rest_args);
        options_item = argc > 0 ? js_array_get_int(rest_args, 0) : make_js_undefined();
        Item last = argc > 0 ? js_array_get_int(rest_args, argc - 1) : make_js_undefined();
        if (get_type_id(last) == LMD_TYPE_FUNC) callback = last;
    }

    int port = 443;
    char host_buf[256] = "localhost";
    bool has_host = false;
    bool has_port = false;
    char local_address[256] = {0};
    bool has_local_address = false;
    bool use_existing_socket = false;

    // extract port, host from options
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item socket_item = js_property_get(options_item, make_string_item("socket"));
        if (get_type_id(socket_item) == LMD_TYPE_MAP || get_type_id(socket_item) == LMD_TYPE_OBJECT ||
            get_type_id(socket_item) == LMD_TYPE_VMAP) {
            use_existing_socket = true;
        }
        Item port_item = js_property_get(options_item, make_string_item("port"));
        if (get_type_id(port_item) == LMD_TYPE_INT) {
            port = (int)it2i(port_item);
            has_port = true;
        }
        Item host_item = js_property_get(options_item, make_string_item("host"));
        if (get_type_id(host_item) == LMD_TYPE_STRING) {
            item_to_cstr(host_item, host_buf, sizeof(host_buf));
            has_host = true;
        }
        Item local_item = js_property_get(options_item, make_string_item("localAddress"));
        if (get_type_id(local_item) == LMD_TYPE_STRING) {
            item_to_cstr(local_item, local_address, sizeof(local_address));
            has_local_address = true;
        }
        Item lookup = js_property_get(options_item, make_string_item("lookup"));
        if (get_type_id(lookup) == LMD_TYPE_FUNC) {
            Item lookup_options = js_new_object();
            js_property_set(lookup_options, make_string_item("family"), make_js_undefined());
            Item hints = js_property_get(options_item, make_string_item("hints"));
            js_property_set(lookup_options, make_string_item("hints"), hints);
            js_property_set(lookup_options, make_string_item("all"), (Item){.item = b2it(true)});
            Item lookup_args[2] = { make_string_item(host_buf), lookup_options };
            js_call_function(lookup, ItemNull, lookup_args, 2);
            js_microtask_flush();
        }
    } else if (get_type_id(options_item) == LMD_TYPE_INT) {
        port = (int)it2i(options_item);
        has_port = true;
    } else if (get_type_id(options_item) == LMD_TYPE_STRING) {
        item_to_cstr(options_item, host_buf, sizeof(host_buf));
        has_host = true;
    }
    if (get_type_id(rest_args) == LMD_TYPE_ARRAY) {
        int64_t argc = js_array_length(rest_args);
        if (argc > 1) {
            Item second = js_array_get_int(rest_args, 1);
            if (get_type_id(second) == LMD_TYPE_STRING && get_type_id(options_item) != LMD_TYPE_MAP) {
                item_to_cstr(second, host_buf, sizeof(host_buf));
                has_host = true;
            }
        }
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
    sock->tcp_initialized = true;
    sock->tls_ctx = ctx;
    sock->owns_context = true;
    sock->is_server = false;
    sock->connect_port = port;
    sock->has_port = has_port;
    sock->has_host = has_host;
    sock->has_local_address = has_local_address;
    memcpy(sock->connect_host, host_buf, sizeof(sock->connect_host));
    memcpy(sock->local_address, local_address, sizeof(sock->local_address));

    Item obj = make_tls_socket_object(sock);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_property_set(obj, make_string_item("__on_secureConnect__"), callback);
    }

    if (use_existing_socket) {
        Item err = make_tls_econnreset_error(sock);
        schedule_tls_error_close(obj, err);
        sock->destroyed = true;
        return obj;
    }

    struct sockaddr_in addr;
    if (uv_ip4_addr(host_buf, port, &addr) != 0) {
        uv_ip4_addr("127.0.0.1", port, &addr);
    }

    uv_connect_t* creq = (uv_connect_t*)mem_calloc(1, sizeof(uv_connect_t), MEM_CAT_JS_RUNTIME);
    creq->data = sock;

    int r = uv_tcp_connect(creq, &sock->tcp, (const struct sockaddr*)&addr, tls_client_connect_cb);
    if (r != 0) {
        log_error("tls: connect: failed: %s", uv_strerror(r));
        mem_free(creq);
        Item err = js_new_error(make_string_item(uv_strerror(r)));
        schedule_tls_error_close(obj, err);
        sock->destroyed = true;
        if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
            uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
                JsTlsSocket* s = (JsTlsSocket*)handle->data;
                if (s) {
                    tls_socket_detach_js_object(s->js_object);
                    if (s->owns_context && s->tls_ctx) tls_context_destroy(s->tls_ctx);
                    mem_free(s);
                }
            });
        }
        return obj;
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
    client->tcp_initialized = true;
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
extern "C" Item js_tls_server_listen(Item port_item, Item host_item, Item callback) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
    if (!srv) return self;

    int port = (int)it2i(port_item);
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

    int r = uv_listen((uv_stream_t*)&srv->tcp, 128, tls_server_connection_cb);
    if (r != 0) {
        log_error("tls: server listen failed: %s", uv_strerror(r));
        return self;
    }

    js_property_set(self, make_string_item("__listening__"), (Item){.item = b2it(true)});
    Item on_listening = js_property_get(self, make_string_item("__on_listening__"));
    if (get_type_id(on_listening) == LMD_TYPE_FUNC) {
        js_call_function(on_listening, self, NULL, 0);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        js_call_function(callback, self, NULL, 0);
    }
    return self;
}

// server.address() — returns {address, family, port} for the listening socket
static Item js_tls_server_address(void) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL) return ItemNull;
    JsTlsServer* srv = (JsTlsServer*)(uintptr_t)it2i(handle_item);
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

static Item js_tls_server_ref(void) { return js_get_this(); }
static Item js_tls_server_unref(void) { return js_get_this(); }

static Item js_tls_server_getConnections(Item callback) {
    Item self = js_get_this();
    if (is_callable(callback)) {
        Item args[2] = { ItemNull, (Item){.item = i2it(0)} };
        js_call_function(callback, self, args, 2);
    }
    return self;
}

// server.close()
extern "C" Item js_tls_server_close(void) {
    Item self = js_get_this();
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
extern "C" Item js_tls_server_on(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char key[64];
    snprintf(key, sizeof(key), "__on_%.*s__", (int)ev->len, ev->chars);
    js_property_set(self, make_string_item(key), callback);
    if (ev->len == 9 && memcmp(ev->chars, "listening", 9) == 0) {
        Item listening = js_property_get(self, make_string_item("__listening__"));
        if (get_type_id(listening) == LMD_TYPE_BOOL && it2b(listening) && is_callable(callback)) {
            js_call_function(callback, self, NULL, 0);
            js_microtask_flush();
        }
    }
    return self;
}

extern "C" Item js_tls_server_once(Item event_item, Item callback) {
    return js_tls_server_on(event_item, callback);
}

extern "C" Item js_tls_createServer(Item options_item, Item handler) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        return js_new_error(make_string_item("No event loop available"));
    }

    // extract cert/key from options
    TlsConfig config = tls_config_default();
    char cert_buf[16384] = {0};
    char key_buf[16384] = {0};

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
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_TLS_SERVER);  // A3-T3b
    js_property_set(obj, make_string_item("__server__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)srv)});
    js_property_set(obj, make_string_item("listen"),
                    js_new_function((void*)js_tls_server_listen, 3));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_tls_server_close, 0));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_tls_server_on, 2));
    js_property_set(obj, make_string_item("once"),
                    js_new_function((void*)js_tls_server_once, 2));
    js_property_set(obj, make_string_item("address"),
                    js_new_function((void*)js_tls_server_address, 0));
    js_property_set(obj, make_string_item("ref"),
                    js_new_function((void*)js_tls_server_ref, 0));
    js_property_set(obj, make_string_item("unref"),
                    js_new_function((void*)js_tls_server_unref, 0));
    js_property_set(obj, make_string_item("getConnections"),
                    js_new_function((void*)js_tls_server_getConnections, 1));

    srv->js_object = obj;
    return obj;
}

// =============================================================================
// tls Module Namespace
// =============================================================================

static Item tls_namespace = {0};

static Item tls_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
    return fn;
}

static Item tls_constructor_prototype(Item ctor, JsClass cls) {
    Item proto_key = make_string_item("prototype");
    Item proto = js_property_get(ctor, proto_key);
    if (get_type_id(proto) != LMD_TYPE_MAP) {
        proto = js_new_object();
        js_property_set(ctor, proto_key, proto);
    }
    js_class_stamp(proto, cls);
    js_property_set(proto, make_string_item("constructor"), ctor);
    js_mark_non_enumerable(proto, make_string_item("constructor"));
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        js_function_set_prototype(ctor, proto);
    }
    return proto;
}

extern "C" Item js_get_tls_namespace(void) {
    if (tls_namespace.item != 0) return tls_namespace;

    tls_namespace = js_new_object();

    tls_set_method(tls_namespace, "connect",             (void*)js_tls_connect, -1);
    tls_set_method(tls_namespace, "createServer",        (void*)js_tls_createServer, 2);
    tls_set_method(tls_namespace, "createSecureContext",  (void*)js_tls_createSecureContext, 1);
    Item server_fn = tls_set_method(tls_namespace, "Server", (void*)js_tls_createServer, 2); // alias
    Item tls_socket_fn = tls_set_method(tls_namespace, "TLSSocket", (void*)js_tls_TLSSocket, 2);

    Item tls_socket_proto = tls_constructor_prototype(tls_socket_fn, JS_CLASS_TLS_SOCKET);
    Item net_ns = js_get_net_namespace();
    Item net_socket_fn = js_property_get(net_ns, make_string_item("Socket"));
    Item net_socket_proto = js_property_get(net_socket_fn, make_string_item("prototype"));
    if (get_type_id(net_socket_fn) == LMD_TYPE_FUNC) {
        js_set_prototype(tls_socket_fn, net_socket_fn);
    }
    if (get_type_id(net_socket_proto) == LMD_TYPE_MAP) {
        js_set_prototype(tls_socket_proto, net_socket_proto);
    }

    Item server_proto = tls_constructor_prototype(server_fn, JS_CLASS_SERVER);
    Item net_server_fn = js_property_get(net_ns, make_string_item("Server"));
    Item net_server_proto = js_property_get(net_server_fn, make_string_item("prototype"));
    if (get_type_id(net_server_fn) == LMD_TYPE_FUNC) {
        js_set_prototype(server_fn, net_server_fn);
    }
    if (get_type_id(net_server_proto) == LMD_TYPE_MAP) {
        js_set_prototype(server_proto, net_server_proto);
    }

    // TLS constants
    js_property_set(tls_namespace, make_string_item("DEFAULT_MIN_VERSION"),
                    make_string_item("TLSv1.2"));
    js_property_set(tls_namespace, make_string_item("DEFAULT_MAX_VERSION"),
                    make_string_item("TLSv1.3"));

    // Cipher suite defaults
    js_property_set(tls_namespace, make_string_item("DEFAULT_CIPHERS"),
        make_string_item("TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"));
    js_property_set(tls_namespace, make_string_item("DEFAULT_ECDH_CURVE"),
        make_string_item("auto"));

    Item default_key = make_string_item("default");
    js_property_set(tls_namespace, default_key, tls_namespace);

    return tls_namespace;
}

extern "C" void js_tls_reset(void) {
    tls_namespace = (Item){0};
}
