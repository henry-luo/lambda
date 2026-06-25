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
#include <math.h>
#include <netinet/in.h>
#include <sys/socket.h>

extern __thread EvalContext* context;
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" void js_clearTimeout(Item timer_id);
extern "C" Item js_buffer_from_bytes(const char* data, int len);
extern "C" void heap_register_gc_root_range(uint64_t* base, int count);
extern "C" Item js_throw_invalid_arg_type(const char* name, const char* expected, Item actual);
extern "C" Item js_throw_out_of_range(const char* name, const char* range, Item actual);
extern "C" Item js_timeout_ref(Item this_val);
extern "C" Item js_timeout_unref(Item this_val);
extern "C" Item js_net_Socket(Item options);

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

static Item net_normalized_args_key(void) {
    return make_string_item("__lambda_net_normalized_args__");
}

static bool net_is_normalized_args(Item value) {
    if (get_type_id(value) != LMD_TYPE_ARRAY) return false;
    return js_is_truthy(js_property_get(value, net_normalized_args_key()));
}

static double net_number_value(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) return (double)it2i(value);
    if (type == LMD_TYPE_INT64) return (double)it2l(value);
    if (type == LMD_TYPE_FLOAT) return it2d(value);
    return 0.0;
}

// =============================================================================
// Socket — wraps uv_tcp_t with event model
// =============================================================================

typedef struct PendingSocketWrite PendingSocketWrite;
typedef struct JsServer JsServer;

static Item make_string_item(const char* str);

#define NET_ACTIVE_SOCKET_MAX 512
#define NET_ACTIVE_SERVER_MAX 128
static Item net_active_sockets[NET_ACTIVE_SOCKET_MAX];
static Item net_active_servers[NET_ACTIVE_SERVER_MAX];
static bool net_active_roots_registered = false;

static void net_active_register_roots(void) {
    if (net_active_roots_registered) return;
    heap_register_gc_root_range((uint64_t*)net_active_sockets, NET_ACTIVE_SOCKET_MAX);
    heap_register_gc_root_range((uint64_t*)net_active_servers, NET_ACTIVE_SERVER_MAX);
    net_active_roots_registered = true;
}

static void net_active_add(Item* list, int max, Item obj) {
    if (!obj.item) return;
    net_active_register_roots();
    for (int i = 0; i < max; i++) {
        if (list[i].item == obj.item) return;
    }
    for (int i = 0; i < max; i++) {
        if (list[i].item == 0) {
            list[i] = obj;
            return;
        }
    }
}

static void net_active_remove(Item* list, int max, Item obj) {
    if (!obj.item) return;
    for (int i = 0; i < max; i++) {
        if (list[i].item == obj.item) {
            list[i] = (Item){0};
            return;
        }
    }
}

extern "C" Item js_net_get_active_handles(void) {
    Item arr = js_array_new(0);
    for (int i = 0; i < NET_ACTIVE_SERVER_MAX; i++) {
        if (net_active_servers[i].item) js_array_push(arr, net_active_servers[i]);
    }
    for (int i = 0; i < NET_ACTIVE_SOCKET_MAX; i++) {
        if (net_active_sockets[i].item) js_array_push(arr, net_active_sockets[i]);
    }
    return arr;
}

extern "C" Item js_net_get_active_resources_info(void) {
    Item arr = js_array_new(0);
    for (int i = 0; i < NET_ACTIVE_SERVER_MAX; i++) {
        if (net_active_servers[i].item) js_array_push(arr, make_string_item("TCPServerWrap"));
    }
    for (int i = 0; i < NET_ACTIVE_SOCKET_MAX; i++) {
        if (net_active_sockets[i].item) js_array_push(arr, make_string_item("TCPSocketWrap"));
    }
    return arr;
}

typedef struct JsSocket {
    uv_tcp_t tcp;
    Item      js_object;     // the JS object representing this socket
    bool      connected;
    bool      destroyed;
    bool      connect_pending;
    bool      is_server_side;
    bool      reading;
    bool      paused;
    bool      finished;
    bool      keep_alive_requested;
    bool      no_delay_requested;
    bool      handle_exposed;
    bool      timeout_timer_active;
    bool      end_after_connect;
    bool      end_callback_set;
    bool      remote_ended;
    bool      close_after_write_drain;
    bool      tos_set;
    bool      abort_handler_set;
    bool      abort_scheduled;
    bool      free_after_connect_pending;
    bool      onread_enabled;
    bool      onread_buffer_factory;
    bool      onread_external_alloc;
    bool      auto_select_family;
    bool      handle_closed_by_user;
    int64_t   bytes_read;
    int64_t   bytes_written;
    int64_t   buffer_size;
    int64_t   high_water_mark;
    int       keep_alive_delay_secs;
    int       type_of_service;
    int       utf8_pending_len;
    int       connect_port;
    int       auto_addr_count;
    int       auto_addr_index;
    char      utf8_pending[4];
    char      connect_host[INET6_ADDRSTRLEN];
    char      auto_addrs[8][INET6_ADDRSTRLEN];
    int       auto_families[8];
    Item      timeout_timer;
    Item      end_callback;
    Item      abort_signal;
    Item      abort_handler;
    Item      onread_buffer_source;
    Item      onread_buffer;
    Item      onread_callback;
    PendingSocketWrite* pending_writes_head;
    PendingSocketWrite* pending_writes_tail;
    JsServer* owner_server;
} JsSocket;

typedef struct SocketWriteReq {
    uv_write_t req;
    char*      data;
    size_t     len;
    JsSocket*  sock;
    Item       callback;
} SocketWriteReq;

typedef struct SocketShutdownReq {
    uv_shutdown_t req;
    JsSocket*     sock;
    Item          callback;
} SocketShutdownReq;

typedef struct SocketBlockedErrorReq {
    uv_timer_t timer;
    JsSocket*  sock;
    char       address[INET6_ADDRSTRLEN];
} SocketBlockedErrorReq;

struct PendingSocketWrite {
    char* data;
    size_t len;
    Item callback;
    PendingSocketWrite* next;
};

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

static void socket_set_once_listener(Item obj, const char* event, Item callback) {
    if (!is_callable(callback)) return;
    socket_set_listener(obj, event, callback);
    char key[64];
    snprintf(key, sizeof(key), "__once_%s__", event);
    js_property_set(obj, make_string_item(key), (Item){.item = ITEM_TRUE});
}

static void socket_close_now(JsSocket* sock);
static void socket_close_reset_now(JsSocket* sock);
static void socket_note_closed(JsSocket* sock);
static void socket_remove_abort_listener(JsSocket* sock);
static void socket_release_after_pending_connect(JsSocket* sock);
static void socket_configure_onread(JsSocket* sock, Item onread);
static Item make_uv_error(int status, const char* syscall, const char* host, int port);
static Item make_socket_handle_object(JsSocket* sock);

static void socket_expose_handle(JsSocket* sock) {
    if (!sock || !sock->js_object.item || sock->handle_exposed) return;
    js_property_set(sock->js_object, make_string_item("_handle"), make_socket_handle_object(sock));
    sock->handle_exposed = true;
}

static void socket_hide_handle(JsSocket* sock) {
    if (!sock || !sock->js_object.item) return;
    js_property_set(sock->js_object, make_string_item("_handle"), ItemNull);
    sock->handle_exposed = false;
}

static void socket_clear_timeout(JsSocket* sock) {
    if (!sock || !sock->timeout_timer_active) return;
    js_clearTimeout(sock->timeout_timer);
    sock->timeout_timer = make_undefined_item();
    sock->timeout_timer_active = false;
    if (sock->js_object.item) {
        js_property_set(sock->js_object, make_string_item("__timeout_timer__"), make_undefined_item());
    }
}

static void socket_update_io_counters(JsSocket* sock) {
    if (!sock) return;
    js_property_set(sock->js_object, make_string_item("bytesRead"),
                    (Item){.item = i2it(sock->bytes_read)});
    js_property_set(sock->js_object, make_string_item("bytesWritten"),
                    (Item){.item = i2it(sock->bytes_written)});
    js_property_set(sock->js_object, make_string_item("bufferSize"),
                    (Item){.item = i2it(sock->buffer_size)});
}

static void socket_update_state_properties(JsSocket* sock) {
    if (!sock || !sock->js_object.item) return;

    bool connecting = sock->connect_pending && !sock->connected && !sock->destroyed;
    bool pending = !sock->connected || sock->destroyed;
    const char* ready_state = "closed";
    if (connecting) {
        ready_state = "opening";
    } else if (sock->connected && !sock->destroyed) {
        ready_state = "open";
    }

    js_property_set(sock->js_object, make_string_item("connecting"),
                    (Item){.item = b2it(connecting)});
    js_property_set(sock->js_object, make_string_item("_connecting"),
                    (Item){.item = b2it(connecting)});
    js_property_set(sock->js_object, make_string_item("pending"),
                    (Item){.item = b2it(pending)});
    js_property_set(sock->js_object, make_string_item("readyState"),
                    make_string_item(ready_state));
}

static void socket_set_address_properties(Item obj, const char* prefix,
                                          const struct sockaddr_storage* addr) {
    if (!obj.item || !addr) return;

    char address[INET6_ADDRSTRLEN];
    const char* family = NULL;
    int port = 0;
    address[0] = '\0';

    if (addr->ss_family == AF_INET) {
        const struct sockaddr_in* a4 = (const struct sockaddr_in*)addr;
        uv_ip4_name(a4, address, sizeof(address));
        family = "IPv4";
        port = ntohs(a4->sin_port);
    } else if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6* a6 = (const struct sockaddr_in6*)addr;
        uv_ip6_name(a6, address, sizeof(address));
        family = "IPv6";
        port = ntohs(a6->sin6_port);
    } else {
        return;
    }

    char key[32];
    snprintf(key, sizeof(key), "%sAddress", prefix);
    js_property_set(obj, make_string_item(key), make_string_item(address));
    snprintf(key, sizeof(key), "%sFamily", prefix);
    js_property_set(obj, make_string_item(key), make_string_item(family));
    snprintf(key, sizeof(key), "%sPort", prefix);
    js_property_set(obj, make_string_item(key), (Item){.item = i2it(port)});
}

static void socket_update_address_properties(JsSocket* sock) {
    if (!sock || !sock->js_object.item) return;

    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    if (uv_tcp_getsockname(&sock->tcp, (struct sockaddr*)&addr, &addrlen) == 0) {
        socket_set_address_properties(sock->js_object, "local", &addr);
    }

    addrlen = sizeof(addr);
    if (uv_tcp_getpeername(&sock->tcp, (struct sockaddr*)&addr, &addrlen) == 0) {
        socket_set_address_properties(sock->js_object, "remote", &addr);
    }
}

// emit event on socket JS object
static void socket_emit(Item obj, const char* event, Item* args, int argc) {
    char key[64];
    snprintf(key, sizeof(key), "__on_%s__", event);
    Item cb = js_property_get(obj, make_string_item(key));
    if (is_callable(cb)) {
        js_call_function(cb, obj, args, argc);
        js_microtask_flush();
        char once_key[64];
        snprintf(once_key, sizeof(once_key), "__once_%s__", event);
        Item once = js_property_get(obj, make_string_item(once_key));
        if (get_type_id(once) == LMD_TYPE_BOOL && it2b(once)) {
            js_property_set(obj, make_string_item(key), make_undefined_item());
            js_property_set(obj, make_string_item(once_key), (Item){.item = ITEM_FALSE});
        }
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

static void socket_emit_close(Item obj, bool had_error) {
    Item args[1] = { (Item){.item = b2it(had_error)} };
    socket_emit(obj, "close", args, 1);
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

static bool socket_uses_utf8_encoding(JsSocket* sock) {
    if (!sock || !sock->js_object.item) return false;
    Item encoding = js_property_get(sock->js_object, make_string_item("__encoding__"));
    if (get_type_id(encoding) != LMD_TYPE_STRING) return false;
    String* s = it2s(encoding);
    if (!s) return false;
    if (s->len == 4 &&
        (s->chars[0] == 'u' || s->chars[0] == 'U') &&
        (s->chars[1] == 't' || s->chars[1] == 'T') &&
        (s->chars[2] == 'f' || s->chars[2] == 'F') &&
        s->chars[3] == '8') {
        return true;
    }
    if (s->len == 5 &&
        (s->chars[0] == 'u' || s->chars[0] == 'U') &&
        (s->chars[1] == 't' || s->chars[1] == 'T') &&
        (s->chars[2] == 'f' || s->chars[2] == 'F') &&
        s->chars[3] == '-' &&
        s->chars[4] == '8') {
        return true;
    }
    return false;
}

static int utf8_sequence_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static int utf8_complete_prefix_len(const char* data, int len) {
    if (!data || len <= 0) return 0;
    int pos = len - 1;
    while (pos >= 0 && (((unsigned char)data[pos] & 0xC0) == 0x80)) {
        pos--;
    }
    if (pos < 0) return 0;
    int expected = utf8_sequence_len((unsigned char)data[pos]);
    int available = len - pos;
    if (expected > available) return pos;
    return len;
}

static bool socket_make_read_data(JsSocket* sock, const char* data, int len, Item* out_data) {
    if (!out_data) return false;
    if (!socket_uses_utf8_encoding(sock)) {
        *out_data = js_buffer_from_bytes(data, len);
        return true;
    }

    int total = sock->utf8_pending_len + len;
    char stack_buf[4096];
    char* combined = total <= (int)sizeof(stack_buf)
        ? stack_buf
        : (char*)mem_alloc((size_t)total, MEM_CAT_JS_RUNTIME);
    if (!combined) return false;

    if (sock->utf8_pending_len > 0) {
        memcpy(combined, sock->utf8_pending, (size_t)sock->utf8_pending_len);
    }
    if (len > 0) {
        memcpy(combined + sock->utf8_pending_len, data, (size_t)len);
    }

    int complete = utf8_complete_prefix_len(combined, total);
    int pending = total - complete;
    if (pending > 0 && pending <= (int)sizeof(sock->utf8_pending)) {
        memcpy(sock->utf8_pending, combined + complete, (size_t)pending);
        sock->utf8_pending_len = pending;
    } else {
        sock->utf8_pending_len = 0;
    }

    if (complete <= 0) {
        if (combined != stack_buf) mem_free(combined);
        return false;
    }

    *out_data = make_string_item(combined, complete);
    if (combined != stack_buf) mem_free(combined);
    return true;
}

static void socket_emit_read_data(JsSocket* sock, const char* data, int len) {
    if (!sock || len <= 0) return;
    Item data_cb = js_property_get(sock->js_object, make_string_item("__on_data__"));
    Item pipe_dest = js_property_get(sock->js_object, make_string_item("__pipe_dest__"));
    bool has_pipe = pipe_dest.item != 0 && pipe_dest.item != ITEM_NULL && !is_undefined_item(pipe_dest);
    if (!is_callable(data_cb) && !has_pipe) return;
    Item chunk = ItemNull;
    if (!socket_make_read_data(sock, data, len, &chunk)) return;
    socket_emit(sock->js_object, "data", &chunk, 1);
    socket_pipe_data(sock->js_object, chunk);
}

static bool socket_onread_buffer_data(Item buffer, char** out_data, size_t* out_len) {
    if (!out_data || !out_len || !js_is_typed_array(buffer)) return false;
    if (js_typed_array_is_out_of_bounds_item(buffer)) return false;
    int byte_len = js_typed_array_byte_length(buffer);
    if (byte_len <= 0) return false;
    void* data = js_typed_array_current_data_ptr(buffer);
    if (!data) return false;
    *out_data = (char*)data;
    *out_len = (size_t)byte_len;
    return true;
}

static bool socket_prepare_onread_buffer(JsSocket* sock, char** out_data, size_t* out_len) {
    if (!sock || !sock->onread_enabled) return false;
    Item buffer = sock->onread_buffer;
    if (sock->onread_buffer_factory) {
        Item produced = js_call_function(sock->onread_buffer_source, sock->js_object, NULL, 0);
        js_microtask_flush();
        if (js_is_typed_array(produced)) {
            sock->onread_buffer = produced;
            buffer = produced;
        }
    }
    return socket_onread_buffer_data(buffer, out_data, out_len);
}

static void socket_emit_onread(JsSocket* sock, int nread) {
    if (!sock || !sock->onread_enabled || !is_callable(sock->onread_callback)) return;
    Item args[2] = {
        (Item){.item = i2it(nread)},
        sock->onread_buffer
    };
    Item result = js_call_function(sock->onread_callback, sock->js_object, args, 2);
    js_microtask_flush();
    if (get_type_id(result) == LMD_TYPE_BOOL && !it2b(result)) {
        sock->paused = true;
        int r = uv_read_stop((uv_stream_t*)&sock->tcp);
        if (r == 0) sock->reading = false;
    }
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

extern "C" Item js_socket_once(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    String* ev = it2s(event_item);
    char event[64];
    int len = ev->len < sizeof(event) - 1 ? (int)ev->len : (int)sizeof(event) - 1;
    memcpy(event, ev->chars, (size_t)len);
    event[len] = '\0';
    socket_set_once_listener(self, event, callback);
    return self;
}

static Item socket_make_error(const char* code, const char* message) {
    Item err = js_new_error(make_string_item(message));
    js_property_set(err, make_string_item("code"), make_string_item(code));
    return err;
}

static void socket_report_write_error(JsSocket* sock, Item callback, Item err) {
    if (is_callable(callback)) {
        js_call_function(callback, make_undefined_item(), &err, 1);
        js_microtask_flush();
    } else if (sock && sock->js_object.item) {
        socket_emit(sock->js_object, "error", &err, 1);
    }
}

static Item socket_emit_error_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    Item err = env[1];
    socket_emit(self, "error", &err, 1);
    return make_undefined_item();
}

static void socket_schedule_error_event(JsSocket* sock, Item err) {
    if (!sock || !sock->js_object.item) return;
    Item* env = js_alloc_env(2);
    env[0] = sock->js_object;
    env[1] = err;
    Item fn = js_new_closure((void*)socket_emit_error_scheduled, 0, env, 2);
    js_next_tick_enqueue(fn);
}

static void socket_update_writable(JsSocket* sock, bool writable) {
    if (!sock || !sock->js_object.item) return;
    js_property_set(sock->js_object, make_string_item("writable"),
                    (Item){.item = b2it(writable)});
}

static void socket_update_readable(JsSocket* sock, bool readable) {
    if (!sock || !sock->js_object.item) return;
    js_property_set(sock->js_object, make_string_item("readable"),
                    (Item){.item = b2it(readable)});
}

static void socket_maybe_close_after_drain(JsSocket* sock) {
    if (!sock || sock->destroyed || !sock->close_after_write_drain) return;
    if (sock->buffer_size > 0) return;
    sock->close_after_write_drain = false;
    socket_close_now(sock);
}

static void socket_handle_remote_eof(JsSocket* sock) {
    if (!sock || sock->destroyed) return;

    sock->remote_ended = true;
    sock->reading = false;
    socket_update_readable(sock, false);

    if (sock->utf8_pending_len > 0) {
        Item data = make_string_item(sock->utf8_pending, sock->utf8_pending_len);
        sock->utf8_pending_len = 0;
        socket_emit(sock->js_object, "data", &data, 1);
        socket_pipe_data(sock->js_object, data);
        if (sock->destroyed) return;
    }

    socket_emit(sock->js_object, "end", NULL, 0);
    socket_pipe_end(sock->js_object);
    if (sock->destroyed) return;

    if (socket_allow_half_open(sock->js_object)) {
        return;
    }

    socket_finish_on_remote_end(sock);
    socket_update_writable(sock, false);
    if (sock->buffer_size > 0) {
        sock->close_after_write_drain = true;
        return;
    }
    socket_close_now(sock);
}

static Item socket_submit_write(JsSocket* sock, char* copy, size_t data_len,
                                Item callback, bool counted_before) {
    SocketWriteReq* wreq = (SocketWriteReq*)mem_calloc(1, sizeof(SocketWriteReq), MEM_CAT_JS_RUNTIME);
    uv_buf_t buf = uv_buf_init(copy, (unsigned int)data_len);
    wreq->data = copy;
    wreq->len = data_len;
    wreq->sock = sock;
    wreq->callback = callback;
    wreq->req.data = wreq;

    if (!counted_before) {
        sock->bytes_written += (int64_t)data_len;
        sock->buffer_size += (int64_t)data_len;
        socket_update_io_counters(sock);
    }

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
            socket_maybe_close_after_drain(wreq->sock);
            if (wreq->data) mem_free(wreq->data);
            mem_free(wreq);
        });

    if (r != 0) {
        sock->bytes_written -= (int64_t)data_len;
        sock->buffer_size -= (int64_t)data_len;
        if (sock->bytes_written < 0) sock->bytes_written = 0;
        if (sock->buffer_size < 0) sock->buffer_size = 0;
        socket_update_io_counters(sock);
        if (is_callable(callback)) {
            Item err = make_uv_error(r, "write", NULL, -1);
            js_call_function(callback, make_undefined_item(), &err, 1);
            js_microtask_flush();
        }
        mem_free(copy);
        mem_free(wreq);
        return (Item){.item = b2it(false)};
    }

    return (Item){.item = b2it(sock->buffer_size <= sock->high_water_mark)};
}

static void socket_queue_write(JsSocket* sock, char* copy, size_t data_len, Item callback) {
    PendingSocketWrite* pending =
        (PendingSocketWrite*)mem_calloc(1, sizeof(PendingSocketWrite), MEM_CAT_JS_RUNTIME);
    pending->data = copy;
    pending->len = data_len;
    pending->callback = callback;
    if (sock->pending_writes_tail) {
        sock->pending_writes_tail->next = pending;
    } else {
        sock->pending_writes_head = pending;
    }
    sock->pending_writes_tail = pending;
    sock->bytes_written += (int64_t)data_len;
    sock->buffer_size += (int64_t)data_len;
    socket_update_io_counters(sock);
}

static void socket_fail_pending_writes(JsSocket* sock, Item err) {
    if (!sock) return;
    PendingSocketWrite* pending = sock->pending_writes_head;
    sock->pending_writes_head = NULL;
    sock->pending_writes_tail = NULL;
    while (pending) {
        PendingSocketWrite* next = pending->next;
        sock->buffer_size -= (int64_t)pending->len;
        if (sock->buffer_size < 0) sock->buffer_size = 0;
        if (is_callable(pending->callback)) {
            js_call_function(pending->callback, make_undefined_item(), &err, 1);
            js_microtask_flush();
        }
        if (pending->data) mem_free(pending->data);
        mem_free(pending);
        pending = next;
    }
    socket_update_io_counters(sock);
}

static bool socket_signal_is_aborted(Item signal) {
    TypeId type = get_type_id(signal);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return false;
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    return get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted);
}

static Item socket_make_abort_error(void) {
    Item err = js_new_error(make_string_item("The operation was aborted"));
    js_property_set(err, make_string_item("name"), make_string_item("AbortError"));
    js_property_set(err, make_string_item("code"), make_string_item("ABORT_ERR"));
    return err;
}

static Item socket_abort_reason(JsSocket* sock) {
    if (sock && sock->abort_signal.item) {
        Item reason = js_property_get(sock->abort_signal, make_string_item("reason"));
        if (!is_undefined_item(reason) && reason.item != ITEM_NULL) return reason;
    }
    return socket_make_abort_error();
}

static Item js_socket_abort_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    JsSocket* sock = socket_from_object(self);
    if (!sock || sock->destroyed) return make_undefined_item();

    sock->abort_scheduled = false;
    Item err = socket_abort_reason(sock);
    socket_remove_abort_listener(sock);
    socket_fail_pending_writes(sock, err);
    socket_emit(self, "error", &err, 1);
    if (!sock->destroyed) socket_close_now(sock);
    return make_undefined_item();
}

static void socket_schedule_abort(JsSocket* sock) {
    if (!sock || sock->destroyed || sock->abort_scheduled) return;
    sock->abort_scheduled = true;
    Item* env = js_alloc_env(1);
    env[0] = sock->js_object;
    Item fn = js_new_closure((void*)js_socket_abort_scheduled, 0, env, 1);
    js_next_tick_enqueue(fn);
}

static Item js_socket_abort_signal_event(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    JsSocket* sock = socket_from_object(self);
    if (sock) socket_schedule_abort(sock);
    return make_undefined_item();
}

static void socket_remove_abort_listener(JsSocket* sock) {
    if (!sock || !sock->abort_handler_set) return;
    Item remove_fn = js_property_get(sock->abort_signal, make_string_item("removeEventListener"));
    if (is_callable(remove_fn)) {
        Item args[2] = { make_string_item("abort"), sock->abort_handler };
        js_call_function(remove_fn, sock->abort_signal, args, 2);
        js_microtask_flush();
    }
    sock->abort_handler_set = false;
    sock->abort_signal = make_undefined_item();
    sock->abort_handler = make_undefined_item();
}

static void socket_release_after_pending_connect(JsSocket* sock) {
    if (!sock || !sock->free_after_connect_pending || sock->connect_pending) return;
    mem_free(sock);
}

static bool socket_configure_abort_signal(JsSocket* sock, Item signal) {
    if (!sock || !sock->js_object.item) return false;
    TypeId type = get_type_id(signal);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return false;

    sock->abort_signal = signal;
    if (socket_signal_is_aborted(signal)) {
        socket_schedule_abort(sock);
        return true;
    }

    Item add_fn = js_property_get(signal, make_string_item("addEventListener"));
    if (!is_callable(add_fn)) return false;

    Item* env = js_alloc_env(1);
    env[0] = sock->js_object;
    Item handler = js_new_closure((void*)js_socket_abort_signal_event, 1, env, 1);
    Item args[2] = { make_string_item("abort"), handler };
    js_call_function(add_fn, signal, args, 2);
    js_microtask_flush();
    sock->abort_handler = handler;
    sock->abort_handler_set = true;
    return false;
}

static void socket_configure_onread(JsSocket* sock, Item onread) {
    if (!sock) return;
    TypeId type = get_type_id(onread);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return;

    Item buffer = js_property_get(onread, make_string_item("buffer"));
    Item callback = js_property_get(onread, make_string_item("callback"));
    if (!is_callable(callback)) return;
    if (!js_is_typed_array(buffer) && !is_callable(buffer)) return;

    sock->onread_enabled = true;
    sock->onread_buffer_factory = is_callable(buffer);
    sock->onread_buffer_source = buffer;
    sock->onread_buffer = buffer;
    sock->onread_callback = callback;
}

static void socket_flush_pending_writes(JsSocket* sock) {
    if (!sock) return;
    PendingSocketWrite* pending = sock->pending_writes_head;
    sock->pending_writes_head = NULL;
    sock->pending_writes_tail = NULL;
    while (pending) {
        PendingSocketWrite* next = pending->next;
        socket_submit_write(sock, pending->data, pending->len, pending->callback, true);
        mem_free(pending);
        pending = next;
    }
}

static Item socket_write_data(Item self, JsSocket* sock, Item data_item, Item callback) {
    if (!sock || sock->destroyed) {
        if (is_callable(callback)) {
            Item err = socket_make_error(
                "ERR_STREAM_DESTROYED",
                "Cannot call write after a stream was destroyed");
            js_call_function(callback, make_undefined_item(), &err, 1);
            js_microtask_flush();
        }
        return (Item){.item = b2it(false)};
    }

    const char* data = NULL;
    size_t data_len = 0;
    if (!socket_get_write_bytes(data_item, &data, &data_len)) {
        if (data_item.item == ITEM_NULL || get_type_id(data_item) == LMD_TYPE_NULL) {
            return js_throw_type_error_code(
                "ERR_STREAM_NULL_VALUES",
                "May not write null values to stream");
        }
        return js_throw_invalid_arg_type(
            "chunk",
            "string or an instance of Buffer, TypedArray, or DataView",
            data_item);
    }

    if (sock->remote_ended && !socket_allow_half_open(self)) {
        Item err = socket_make_error("EPIPE", "This socket has been ended by the other party");
        socket_report_write_error(sock, callback, err);
        socket_schedule_error_event(sock, err);
        return (Item){.item = b2it(false)};
    }
    Item handle = js_property_get(self, make_string_item("_handle"));
    if (sock->handle_closed_by_user || handle.item == ITEM_NULL || is_undefined_item(handle)) {
        Item err = ItemNull;
        if (handle.item == ITEM_NULL || is_undefined_item(handle)) {
            err = socket_make_error("ERR_SOCKET_CLOSED", "Socket is closed");
        } else {
            err = make_uv_error(UV_EBADF, "write", NULL, -1);
        }
        socket_report_write_error(sock, callback, err);
        return (Item){.item = b2it(false)};
    }

    char* copy = (char*)mem_alloc(data_len, MEM_CAT_JS_RUNTIME);
    memcpy(copy, data, data_len);
    if (sock->connect_pending && !sock->connected) {
        socket_queue_write(sock, copy, data_len, callback);
        return (Item){.item = b2it(sock->buffer_size <= sock->high_water_mark)};
    }

    return socket_submit_write(sock, copy, data_len, callback, false);
}

// write(data[, encoding][, callback]) — write to socket
extern "C" Item js_socket_write(Item rest_args) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 16 ? 16 : (int)argc64;
    Item data_item = argc > 0 ? js_array_get_int(rest_args, 0) : make_undefined_item();
    Item callback = make_undefined_item();
    for (int i = argc - 1; i >= 1; i--) {
        Item arg = js_array_get_int(rest_args, i);
        if (is_callable(arg)) {
            callback = arg;
            break;
        }
    }
    return socket_write_data(self, sock, data_item, callback);
}

static void socket_shutdown_writes(JsSocket* sock, Item callback) {
    if (!sock || sock->destroyed || !sock->connected) return;
    SocketShutdownReq* sreq =
        (SocketShutdownReq*)mem_calloc(1, sizeof(SocketShutdownReq), MEM_CAT_JS_RUNTIME);
    sreq->sock = sock;
    sreq->callback = callback;
    sreq->req.data = sreq;
    int r = uv_shutdown(&sreq->req, (uv_stream_t*)&sock->tcp,
        [](uv_shutdown_t* req, int status) {
            SocketShutdownReq* sreq = (SocketShutdownReq*)req->data;
            JsSocket* sock = sreq ? sreq->sock : NULL;
            if (sock && !sock->destroyed) {
                socket_emit_finish_once(sock);
                if (is_callable(sreq->callback)) {
                    js_call_function(sreq->callback, make_undefined_item(), NULL, 0);
                    js_microtask_flush();
                }
                if (sock->remote_ended) {
                    socket_close_now(sock);
                }
            }
            if (sreq) mem_free(sreq);
        });
    if (r != 0) {
        socket_emit_finish_once(sock);
        if (is_callable(callback)) {
            js_call_function(callback, make_undefined_item(), NULL, 0);
            js_microtask_flush();
        }
        mem_free(sreq);
        if (sock->remote_ended) socket_close_now(sock);
    }
}

// end([data][, encoding][, callback]) — half-close (shutdown write side)
extern "C" Item js_socket_end(Item rest_args) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    Item callback = make_undefined_item();
    Item data_item = make_undefined_item();
    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 16 ? 16 : (int)argc64;
    if (argc > 0) data_item = js_array_get_int(rest_args, 0);
    if (is_callable(data_item)) {
        callback = data_item;
        data_item = make_undefined_item();
    } else {
        for (int i = argc - 1; i >= 1; i--) {
            Item arg = js_array_get_int(rest_args, i);
            if (is_callable(arg)) {
                callback = arg;
                break;
            }
        }
        if (!is_undefined_item(data_item) && data_item.item != ITEM_NULL) {
            socket_write_data(self, sock, data_item, make_undefined_item());
        }
    }
    if (!sock || sock->destroyed) return self;
    socket_update_writable(sock, false);

    if (sock->connect_pending && !sock->connected) {
        sock->end_after_connect = true;
        sock->end_callback = callback;
        sock->end_callback_set = is_callable(callback);
        return self;
    }

    if (!sock->connected) {
        socket_emit_finish_once(sock);
        if (is_callable(callback)) {
            js_call_function(callback, make_undefined_item(), NULL, 0);
            js_microtask_flush();
        }
        return self;
    }

    socket_shutdown_writes(sock, callback);

    return self;
}

// destroy() — close socket
extern "C" Item js_socket_destroy(Item error_item) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (!sock || sock->destroyed) return self;

    sock->destroyed = true;
    socket_update_writable(sock, false);
    if (sock->connect_pending) {
        Item err = socket_make_error(
            "ERR_SOCKET_CLOSED_BEFORE_CONNECTION",
            "Socket closed before connection was established");
        socket_fail_pending_writes(sock, err);
    }
    js_property_set(self, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
    socket_update_state_properties(sock);
    if (!is_undefined_item(error_item) && error_item.item != ITEM_NULL) {
        socket_emit(self, "error", &error_item, 1);
    }
    if (sock->connect_pending) return self;
    socket_close_now(sock);
    return self;
}

extern "C" Item js_socket_resetAndDestroy(Item error_item) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (!sock || sock->destroyed) return self;

    sock->destroyed = true;
    socket_update_writable(sock, false);
    if (!is_undefined_item(error_item) && error_item.item != ITEM_NULL) {
        socket_emit(self, "error", &error_item, 1);
    }
    socket_close_reset_now(sock);
    return self;
}

static void socket_close_now(JsSocket* sock) {
    if (!sock) return;
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        sock->reading = false;
        sock->destroyed = true;
        socket_clear_timeout(sock);
        socket_remove_abort_listener(sock);
        socket_hide_handle(sock);
        socket_update_writable(sock, false);
        js_property_set(sock->js_object, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
        socket_update_state_properties(sock);
        uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
            JsSocket* s = (JsSocket*)handle->data;
            if (s) {
                net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, s->js_object);
                js_property_set(s->js_object, make_string_item("__handle__"), ItemNull);
                socket_emit_close(s->js_object, false);
                socket_note_closed(s);
                if (s->connect_pending) {
                    s->free_after_connect_pending = true;
                } else {
                    mem_free(s);
                }
            }
        });
    }
}

static void socket_close_reset_now(JsSocket* sock) {
    if (!sock) return;
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        sock->reading = false;
        sock->destroyed = true;
        socket_clear_timeout(sock);
        socket_remove_abort_listener(sock);
        socket_hide_handle(sock);
        socket_update_writable(sock, false);
        js_property_set(sock->js_object, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
        socket_update_state_properties(sock);
        int r = uv_tcp_close_reset(&sock->tcp, [](uv_handle_t* handle) {
            JsSocket* s = (JsSocket*)handle->data;
            if (s) {
                    net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, s->js_object);
                    js_property_set(s->js_object, make_string_item("__handle__"), ItemNull);
                    socket_emit_close(s->js_object, false);
                    socket_note_closed(s);
                    if (s->connect_pending) {
                        s->free_after_connect_pending = true;
                    } else {
                        mem_free(s);
                    }
                }
            });
        if (r != 0 && !uv_is_closing((uv_handle_t*)&sock->tcp)) {
            uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
                JsSocket* s = (JsSocket*)handle->data;
                if (s) {
                    net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, s->js_object);
                    js_property_set(s->js_object, make_string_item("__handle__"), ItemNull);
                    socket_emit_close(s->js_object, false);
                    socket_note_closed(s);
                    if (s->connect_pending) {
                        s->free_after_connect_pending = true;
                    } else {
                        mem_free(s);
                    }
                }
            });
        }
    }
}

static Item js_socket_timeout_fire(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    JsSocket* sock = socket_from_object(self);
    if (!sock || sock->destroyed) return make_undefined_item();
    socket_emit(self, "timeout", NULL, 0);
    return make_undefined_item();
}

// Socket.setTimeout(msecs, callback) — set idle timeout
static Item js_socket_setTimeout(Item msecs, Item callback) {
    Item self = js_get_this();
    TypeId delay_type = get_type_id(msecs);
    if (delay_type != LMD_TYPE_INT && delay_type != LMD_TYPE_INT64 && delay_type != LMD_TYPE_FLOAT) {
        return js_throw_invalid_arg_type("msecs", "number", msecs);
    }
    Item delay_num = js_to_number(msecs);
    double delay = net_number_value(delay_num);
    if (!isfinite(delay) || delay < 0) {
        return js_throw_out_of_range("msecs", "a non-negative finite number", msecs);
    }
    if (!is_undefined_item(callback) && !is_callable(callback)) {
        return js_throw_invalid_arg_type("callback", "function", callback);
    }

    js_property_set(self, make_string_item("timeout"), msecs);
    if (is_callable(callback)) {
        Item args[] = { make_string_item("timeout"), callback };
        Item on_fn = js_property_get(self, make_string_item("on"));
        if (is_callable(on_fn)) {
            js_call_function(on_fn, self, args, 2);
        }
    }
    JsSocket* sock = socket_from_object(self);
    socket_clear_timeout(sock);
    if (delay > 0) {
        Item* env = js_alloc_env(1);
        env[0] = self;
        Item fn = js_new_closure((void*)js_socket_timeout_fire, 0, env, 1);
        Item timer = js_setTimeout(fn, (Item){.item = i2it((int64_t)delay)});
        if (sock) {
            sock->timeout_timer = timer;
            sock->timeout_timer_active = true;
            js_property_set(self, make_string_item("__timeout_timer__"), timer);
            if (!uv_is_closing((uv_handle_t*)&sock->tcp) &&
                !uv_has_ref((uv_handle_t*)&sock->tcp)) {
                js_timeout_unref(timer);
            }
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

// Socket.setNoDelay([noDelay])
static Item js_socket_setNoDelay(Item noDelay) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    bool enable = is_undefined_item(noDelay) ? true : js_is_truthy(noDelay);
    if (sock) {
        if (sock->no_delay_requested == enable) return self;
        sock->no_delay_requested = enable;
    } else if (!enable) {
        return self;
    }

    Item handle = js_property_get(self, make_string_item("_handle"));
    if (handle.item != 0 && handle.item != ITEM_NULL && !is_undefined_item(handle)) {
        Item fn = js_property_get(handle, make_string_item("setNoDelay"));
        if (is_callable(fn)) {
            Item arg = (Item){.item = b2it(enable)};
            js_call_function(fn, handle, &arg, 1);
            js_microtask_flush();
        }
    }
    return self;
}

// Socket.ref() / Socket.unref() — stub
static Item js_socket_ref(void) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (sock && !uv_is_closing((uv_handle_t*)&sock->tcp)) {
        uv_ref((uv_handle_t*)&sock->tcp);
        if (sock->timeout_timer_active) js_timeout_ref(sock->timeout_timer);
    }
    return self;
}

static Item js_socket_unref(void) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (sock && !uv_is_closing((uv_handle_t*)&sock->tcp)) {
        uv_unref((uv_handle_t*)&sock->tcp);
        if (sock->timeout_timer_active) js_timeout_unref(sock->timeout_timer);
    }
    return self;
}
static Item js_socket_cork(void) { return js_get_this(); }
static Item js_socket_uncork(void) { return js_get_this(); }

static JsSocket* socket_from_handle_object(Item self) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__socket_handle__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (JsSocket*)(uintptr_t)it2i(handle_item);
}

static Item js_socket_handle_setKeepAlive(Item enable_item, Item delay_item) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_handle_object(self);
    if (!sock) return make_undefined_item();

    int delay_secs = 0;
    if (!is_undefined_item(delay_item) && delay_item.item != ITEM_NULL) {
        Item num = js_to_number(delay_item);
        double d = net_number_value(num);
        if (d > 0) delay_secs = (int)(d / 1000.0);
    }
    uv_tcp_keepalive(&sock->tcp, js_is_truthy(enable_item) ? 1 : 0, (unsigned int)delay_secs);
    return make_undefined_item();
}

static Item js_socket_handle_setNoDelay(Item enable_item) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_handle_object(self);
    if (!sock) return make_undefined_item();
    uv_tcp_nodelay(&sock->tcp, js_is_truthy(enable_item) ? 1 : 0);
    return make_undefined_item();
}

static Item js_socket_handle_close(void) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_handle_object(self);
    if (!sock) return make_undefined_item();
    sock->handle_closed_by_user = true;
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        uv_close((uv_handle_t*)&sock->tcp, NULL);
    }
    return make_undefined_item();
}

static Item js_socket_setEncoding(Item encoding) {
    Item self = js_get_this();
    js_property_set(self, make_string_item("__encoding__"), encoding);
    return self;
}

static void socket_apply_type_of_service(JsSocket* sock) {
    if (!sock || !sock->tos_set || uv_is_closing((uv_handle_t*)&sock->tcp)) return;
    uv_os_fd_t fd;
    if (uv_fileno((const uv_handle_t*)&sock->tcp, &fd) != 0) return;
    int value = sock->type_of_service;
    setsockopt(fd, IPPROTO_IP, IP_TOS, &value, sizeof(value));
#ifdef IPV6_TCLASS
    setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &value, sizeof(value));
#endif
}

static bool socket_read_uint8(Item value, int* out_value) {
    TypeId type = get_type_id(value);
    double n = 0;
    if (type == LMD_TYPE_INT) {
        n = (double)it2i(value);
    } else if (type == LMD_TYPE_INT64) {
        n = (double)it2l(value);
    } else if (type == LMD_TYPE_FLOAT) {
        n = it2d(value);
        if (n != n) {
            js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                                     "The \"tos\" argument must be of type number.");
            return false;
        }
    } else {
        js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                                 "The \"tos\" argument must be of type number.");
        return false;
    }
    if (n < 0 || n > 255) {
        js_throw_range_error_code("ERR_OUT_OF_RANGE",
                                  "The value of \"tos\" is out of range.");
        return false;
    }
    *out_value = (int)n;
    return true;
}

static Item js_socket_setTypeOfService(Item tos_item) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    int tos = 0;
    if (!socket_read_uint8(tos_item, &tos)) return make_undefined_item();
    if (sock) {
        sock->type_of_service = tos;
        sock->tos_set = true;
        socket_apply_type_of_service(sock);
    }
    return self;
}

static Item js_socket_getTypeOfService(void) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (!sock || !sock->tos_set) return (Item){.item = i2it(0)};
    return (Item){.item = i2it(sock->type_of_service)};
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
    if (sock) {
        sock->paused = false;
        if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
            uv_ref((uv_handle_t*)&sock->tcp);
        }
    }
    socket_start_read(sock);
    return self;
}

// Socket.pause() — stop libuv reads until the socket is destroyed/resumed.
static Item js_socket_pause(void) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    if (sock && !sock->destroyed) {
        sock->paused = true;
        int r = uv_read_stop((uv_stream_t*)&sock->tcp);
        if (r == 0) sock->reading = false;
        if (!sock->connected && !uv_is_closing((uv_handle_t*)&sock->tcp)) {
            uv_unref((uv_handle_t*)&sock->tcp);
        }
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

static Item net_socket_prototype = {0};
static Item net_server_prototype = {0};
static Item net_socket_connect_fn = {0};
static bool net_default_auto_select_family = false;
static int net_auto_select_family_timeout = 250; // Node.js default

typedef struct NetBlockListEntry {
    int family;
    int prefix;
    uint32_t addr4;
    unsigned char addr6[16];
} NetBlockListEntry;

#define NET_BLOCK_LIST_MAX 128
#define NET_BLOCK_LIST_INSTANCE_MAX 256

typedef struct NetBlockList {
    int count;
    NetBlockListEntry entries[NET_BLOCK_LIST_MAX];
} NetBlockList;

static NetBlockList net_block_list_instances[NET_BLOCK_LIST_INSTANCE_MAX];
static int net_block_list_instance_count = 0;

static NetBlockList* net_block_list_alloc(void) {
    if (net_block_list_instance_count >= NET_BLOCK_LIST_INSTANCE_MAX) return NULL;
    NetBlockList* list = &net_block_list_instances[net_block_list_instance_count++];
    memset(list, 0, sizeof(NetBlockList));
    return list;
}

static Item make_socket_handle_object(JsSocket* sock) {
    Item handle = js_new_object();
    js_property_set(handle, make_string_item("__socket_handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)sock)});
    js_property_set(handle, make_string_item("setKeepAlive"),
                    js_new_function((void*)js_socket_handle_setKeepAlive, 2));
    js_property_set(handle, make_string_item("setNoDelay"),
                    js_new_function((void*)js_socket_handle_setNoDelay, 1));
    js_property_set(handle, make_string_item("close"),
                    js_new_function((void*)js_socket_handle_close, 0));
    return handle;
}

// create a JS socket object wrapping a JsSocket
static Item make_socket_object(JsSocket* sock, bool expose_handle) {
    if (sock->high_water_mark < 0) sock->high_water_mark = 16 * 1024;
    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_SOCKET);  // A3-T3b
    if (get_type_id(net_socket_prototype) == LMD_TYPE_MAP) {
        js_set_prototype(obj, net_socket_prototype);
    }
    js_property_set(obj, make_string_item("__handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)sock)});
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_socket_on, 2));
    js_property_set(obj, make_string_item("once"),
                    js_new_function((void*)js_socket_once, 2));
    js_property_set(obj, make_string_item("write"),
                    js_new_function((void*)js_socket_write, -1));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_socket_end, -1));
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_socket_destroy, 1));
    js_property_set(obj, make_string_item("resetAndDestroy"),
                    js_new_function((void*)js_socket_resetAndDestroy, 1));
    // Additional Socket properties
    js_property_set(obj, make_string_item("readable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("writable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = ITEM_FALSE});
    js_property_set(obj, make_string_item("bytesRead"), (Item){.item = i2it(0)});
    js_property_set(obj, make_string_item("bytesWritten"), (Item){.item = i2it(0)});
    js_property_set(obj, make_string_item("bufferSize"), (Item){.item = i2it(0)});
    Item hwm = (Item){.item = i2it(sock->high_water_mark)};
    Item readable_state = js_new_object();
    js_property_set(readable_state, make_string_item("highWaterMark"), hwm);
    Item writable_state = js_new_object();
    js_property_set(writable_state, make_string_item("highWaterMark"), hwm);
    js_property_set(obj, make_string_item("_readableState"), readable_state);
    js_property_set(obj, make_string_item("_writableState"), writable_state);
    js_property_set(obj, make_string_item("readableHighWaterMark"), hwm);
    js_property_set(obj, make_string_item("writableHighWaterMark"), hwm);
    js_property_set(obj, make_string_item("connecting"), (Item){.item = ITEM_FALSE});
    js_property_set(obj, make_string_item("_connecting"), (Item){.item = ITEM_FALSE});
    js_property_set(obj, make_string_item("pending"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("readyState"), make_string_item("closed"));
    if (expose_handle) {
        js_property_set(obj, make_string_item("_handle"), make_socket_handle_object(sock));
        sock->handle_exposed = true;
    } else {
        js_property_set(obj, make_string_item("_handle"), ItemNull);
        sock->handle_exposed = false;
    }
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
    js_property_set(obj, make_string_item("setTypeOfService"),
                    js_new_function((void*)js_socket_setTypeOfService, 1));
    js_property_set(obj, make_string_item("getTypeOfService"),
                    js_new_function((void*)js_socket_getTypeOfService, 0));
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
    net_active_add(net_active_sockets, NET_ACTIVE_SOCKET_MAX, obj);
    return obj;
}

// =============================================================================
// net.createConnection(port, host) — TCP client
// =============================================================================

typedef struct NetConnectOptions {
    int port;
    int family;
    char host[256];
    bool has_local_address;
    bool has_local_port;
    int local_port;
    char local_address[256];
    bool keep_alive;
    bool no_delay;
    int keep_alive_delay_secs;
    Item callback;
    Item signal;
    bool has_signal;
    Item onread;
    bool has_onread;
    Item lookup;
    bool has_lookup;
    Item block_list;
    bool has_block_list;
    bool auto_select_family;
    bool auto_select_family_set;
    bool allow_half_open;
} NetConnectOptions;

typedef struct NetResolveReq {
    uv_getaddrinfo_t req;
    JsSocket* sock;
    int port;
    int family;
    char host[256];
    char service[16];
    bool has_local_address;
    bool has_local_port;
    int local_port;
    char local_address[256];
    bool auto_select_family;
    Item lookup;
    Item block_list;
    bool has_block_list;
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
    if (type == LMD_TYPE_INT64) {
        int64_t p = it2l(value);
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

static bool string_item_has_nul(Item value) {
    if (get_type_id(value) != LMD_TYPE_STRING) return false;
    String* s = it2s(value);
    for (int64_t i = 0; i < s->len; i++) {
        if (s->chars[i] == '\0') return true;
    }
    return false;
}

static bool validate_host_string(Item value, const char* name) {
    if (string_item_has_nul(value)) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "The property '%s' must be a string without null bytes.", name);
        js_throw_type_error_code("ERR_INVALID_ARG_VALUE", msg);
        return false;
    }
    return true;
}

static bool net_string_equals_ascii_ci(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static NetBlockList* net_block_list_from_item(Item self) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__net_block_list__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (NetBlockList*)(uintptr_t)it2i(handle_item);
}

static bool net_block_list_type_family(Item type_item, int* family) {
    if (!family) return false;
    if (is_undefined_item(type_item) || type_item.item == ITEM_NULL) return true;
    char type_buf[16];
    if (!copy_string_item(type_item, type_buf, (int)sizeof(type_buf))) return false;
    if (net_string_equals_ascii_ci(type_buf, "ipv4")) {
        *family = 4;
        return true;
    }
    if (net_string_equals_ascii_ci(type_buf, "ipv6")) {
        *family = 6;
        return true;
    }
    return false;
}

static bool net_block_list_parse_address(Item address_item, Item type_item, NetBlockListEntry* entry) {
    if (!entry) return false;
    char address[INET6_ADDRSTRLEN];
    if (!copy_string_item(address_item, address, (int)sizeof(address))) return false;

    int requested_family = 0;
    if (!net_block_list_type_family(type_item, &requested_family)) return false;

    struct sockaddr_in addr4;
    if ((requested_family == 0 || requested_family == 4) &&
        uv_ip4_addr(address, 0, &addr4) == 0) {
        memset(entry, 0, sizeof(NetBlockListEntry));
        entry->family = 4;
        entry->prefix = 32;
        entry->addr4 = ntohl(addr4.sin_addr.s_addr);
        return true;
    }

    struct sockaddr_in6 addr6;
    if ((requested_family == 0 || requested_family == 6) &&
        uv_ip6_addr(address, 0, &addr6) == 0) {
        memset(entry, 0, sizeof(NetBlockListEntry));
        entry->family = 6;
        entry->prefix = 128;
        memcpy(entry->addr6, addr6.sin6_addr.s6_addr, sizeof(entry->addr6));
        return true;
    }

    return false;
}

static bool net_block_list_parse_prefix(Item prefix_item, int family, int* prefix) {
    if (!prefix) return false;
    Item number = js_to_number(prefix_item);
    double value = net_number_value(number);
    int max_prefix = family == 6 ? 128 : 32;
    if (value != value || value < 0 || value > max_prefix || value != (int)value) return false;
    *prefix = (int)value;
    return true;
}

static bool net_block_list_match_ipv4(uint32_t addr, uint32_t rule_addr, int prefix) {
    if (prefix <= 0) return true;
    uint32_t mask = prefix == 32 ? 0xffffffffu : (0xffffffffu << (32 - prefix));
    return (addr & mask) == (rule_addr & mask);
}

static bool net_block_list_match_ipv6(const unsigned char* addr, const unsigned char* rule_addr, int prefix) {
    if (prefix <= 0) return true;
    int whole_bytes = prefix / 8;
    int rem_bits = prefix % 8;
    for (int i = 0; i < whole_bytes; i++) {
        if (addr[i] != rule_addr[i]) return false;
    }
    if (rem_bits == 0) return true;
    unsigned char mask = (unsigned char)(0xffu << (8 - rem_bits));
    return (addr[whole_bytes] & mask) == (rule_addr[whole_bytes] & mask);
}

static bool net_block_list_check_parsed(NetBlockList* list, const NetBlockListEntry* address) {
    if (!list || !address) return false;
    for (int i = 0; i < list->count; i++) {
        NetBlockListEntry* rule = &list->entries[i];
        if (rule->family != address->family) continue;
        if (address->family == 4 &&
            net_block_list_match_ipv4(address->addr4, rule->addr4, rule->prefix)) {
            return true;
        }
        if (address->family == 6 &&
            net_block_list_match_ipv6(address->addr6, rule->addr6, rule->prefix)) {
            return true;
        }
    }
    return false;
}

static bool net_block_list_blocks_item(Item list_item, const char* address, int family) {
    NetBlockList* list = net_block_list_from_item(list_item);
    if (!list || !address || !address[0]) return false;
    NetBlockListEntry parsed;
    Item type_item = make_undefined_item();
    if (family == 4) type_item = make_string_item("ipv4");
    else if (family == 6) type_item = make_string_item("ipv6");
    if (!net_block_list_parse_address(make_string_item(address), type_item, &parsed)) return false;
    return net_block_list_check_parsed(list, &parsed);
}

static Item make_ip_blocked_error(const char* address) {
    char msg[256];
    snprintf(msg, sizeof(msg), "IP address %s is blocked", address ? address : "");
    Item err = js_new_error(make_string_item(msg));
    js_property_set(err, make_string_item("code"), make_string_item("ERR_IP_BLOCKED"));
    if (address && address[0]) js_property_set(err, make_string_item("address"), make_string_item(address));
    return err;
}

static void socket_emit_ip_blocked_timer_cb(uv_timer_t* timer) {
    SocketBlockedErrorReq* req = timer ? (SocketBlockedErrorReq*)timer->data : NULL;
    if (!req) return;
    JsSocket* sock = req->sock;
    if (sock && !sock->destroyed) {
        Item err = make_ip_blocked_error(req->address);
        socket_fail_pending_writes(sock, err);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
    }
    uv_timer_stop(timer);
    uv_close((uv_handle_t*)timer, [](uv_handle_t* handle) {
        SocketBlockedErrorReq* done = handle ? (SocketBlockedErrorReq*)handle->data : NULL;
        if (done) mem_free(done);
    });
}

static void socket_fail_ip_blocked(JsSocket* sock, const char* address) {
    if (!sock || sock->destroyed) return;
    sock->connect_pending = false;
    socket_update_state_properties(sock);
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return;
    SocketBlockedErrorReq* req =
        (SocketBlockedErrorReq*)mem_calloc(1, sizeof(SocketBlockedErrorReq), MEM_CAT_JS_RUNTIME);
    req->sock = sock;
    if (address) {
        snprintf(req->address, sizeof(req->address), "%s", address);
    }
    if (uv_timer_init(loop, &req->timer) != 0) {
        mem_free(req);
        return;
    }
    req->timer.data = req;
    uv_timer_start(&req->timer, socket_emit_ip_blocked_timer_cb, 0, 0);
}

static int net_keep_alive_delay_secs(Item value) {
    if (is_undefined_item(value) || value.item == ITEM_NULL) return 0;
    Item num = js_to_number(value);
    double d = net_number_value(num);
    if (d <= 0) return 0;
    return (int)(d / 1000.0);
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
        if (!validate_host_string(host, "options.host")) return false;
    }

    Item family = js_property_get(options, make_string_item("family"));
    if (!is_undefined_item(family) && family.item != ITEM_NULL && get_type_id(family) == LMD_TYPE_INT) {
        out->family = (int)it2i(family);
    }

    Item allow_half_open = js_property_get(options, make_string_item("allowHalfOpen"));
    out->allow_half_open = get_type_id(allow_half_open) == LMD_TYPE_BOOL && it2b(allow_half_open);

    Item keep_alive = js_property_get(options, make_string_item("keepAlive"));
    out->keep_alive = js_is_truthy(keep_alive);
    out->keep_alive_delay_secs =
        net_keep_alive_delay_secs(js_property_get(options, make_string_item("keepAliveInitialDelay")));

    Item no_delay = js_property_get(options, make_string_item("noDelay"));
    out->no_delay = js_is_truthy(no_delay);

    Item local_address = js_property_get(options, make_string_item("localAddress"));
    if (!is_undefined_item(local_address) && local_address.item != ITEM_NULL) {
        if (!copy_string_item(local_address, out->local_address, (int)sizeof(out->local_address))) {
            js_throw_invalid_arg_type("options.localAddress", "string", local_address);
            return false;
        }
        out->has_local_address = true;
    }

    Item local_port = js_property_get(options, make_string_item("localPort"));
    if (!is_undefined_item(local_port) && local_port.item != ITEM_NULL) {
        if (!parse_port(local_port, &out->local_port)) return false;
        out->has_local_port = true;
    }

    Item signal = js_property_get(options, make_string_item("signal"));
    if (!is_undefined_item(signal) && signal.item != ITEM_NULL) {
        out->signal = signal;
        out->has_signal = true;
    }

    Item onread = js_property_get(options, make_string_item("onread"));
    if (!is_undefined_item(onread) && onread.item != ITEM_NULL) {
        out->onread = onread;
        out->has_onread = true;
    }

    Item lookup = js_property_get(options, make_string_item("lookup"));
    if (is_callable(lookup)) {
        out->lookup = lookup;
        out->has_lookup = true;
    }

    Item block_list = js_property_get(options, make_string_item("blockList"));
    if (!is_undefined_item(block_list) && block_list.item != ITEM_NULL) {
        out->block_list = block_list;
        out->has_block_list = net_block_list_from_item(block_list) != NULL;
    }

    Item auto_select = js_property_get(options, make_string_item("autoSelectFamily"));
    if (get_type_id(auto_select) == LMD_TYPE_BOOL) {
        out->auto_select_family = it2b(auto_select);
        out->auto_select_family_set = true;
    }
    return true;
}

static bool normalize_connect_args(Item rest_args, NetConnectOptions* out) {
    out->port = 0;
    out->family = 0;
    out->host[0] = '\0';
    memcpy(out->host, "127.0.0.1", 10);
    out->has_local_address = false;
    out->has_local_port = false;
    out->local_port = 0;
    out->local_address[0] = '\0';
    out->keep_alive = false;
    out->no_delay = false;
    out->keep_alive_delay_secs = 0;
    out->callback = make_undefined_item();
    out->signal = make_undefined_item();
    out->has_signal = false;
    out->onread = make_undefined_item();
    out->has_onread = false;
    out->lookup = make_undefined_item();
    out->has_lookup = false;
    out->block_list = make_undefined_item();
    out->has_block_list = false;
    out->auto_select_family = false;
    out->auto_select_family_set = false;
    out->allow_half_open = false;

    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 16 ? 16 : (int)argc64;
    if (argc <= 0) {
        throw_missing_connect_args();
        return false;
    }

    Item first = js_array_get_int(rest_args, 0);
    if (argc == 1 && get_type_id(first) == LMD_TYPE_ARRAY) {
        if (!net_is_normalized_args(first)) {
            throw_missing_connect_args();
            return false;
        }
        rest_args = first;
        argc64 = js_array_length(rest_args);
        argc = argc64 > 16 ? 16 : (int)argc64;
        first = js_array_get_int(rest_args, 0);
    }

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
            if (!validate_host_string(second, "host")) return false;
        }
    }
    if (argc > 2) {
        Item third = js_array_get_int(rest_args, 2);
        if (is_callable(third)) out->callback = third;
    }
    return true;
}

static void client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    JsSocket* sock = (JsSocket*)handle->data;
    if (sock) {
        char* data = NULL;
        size_t len = 0;
        if (socket_prepare_onread_buffer(sock, &data, &len)) {
            sock->onread_external_alloc = true;
            buf->base = data;
            buf->len = len;
            return;
        }
        sock->onread_external_alloc = false;
    }
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSocket* sock = (JsSocket*)stream->data;
    if (nread > 0 && sock) {
        sock->bytes_read += (int64_t)nread;
        socket_update_io_counters(sock);
        if (sock->onread_enabled) {
            socket_emit_onread(sock, (int)nread);
        } else {
            socket_emit_read_data(sock, buf->base, (int)nread);
        }
    }
    if (buf->base && (!sock || !sock->onread_external_alloc)) mem_free(buf->base);
    if (sock) sock->onread_external_alloc = false;
    if (nread < 0) {
        if (sock) {
            sock->reading = false;
            if (nread != UV_EOF) {
                Item err = make_uv_error((int)nread, "read", NULL, -1);
                socket_emit(sock->js_object, "error", &err, 1);
                if (sock->destroyed) return;
            }
            socket_handle_remote_eof(sock);
        }
    }
}

static int socket_connect_resolved(JsSocket* sock, const struct sockaddr* addr) {
    if (!sock || sock->destroyed) return 0;
    sock->connect_host[0] = '\0';
    sock->connect_port = -1;
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in* sa = (const struct sockaddr_in*)addr;
        uv_ip4_name(sa, sock->connect_host, sizeof(sock->connect_host));
        sock->connect_port = ntohs(sa->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6* sa = (const struct sockaddr_in6*)addr;
        uv_ip6_name(sa, sock->connect_host, sizeof(sock->connect_host));
        sock->connect_port = ntohs(sa->sin6_port);
    }
    uv_connect_t* creq = (uv_connect_t*)mem_calloc(1, sizeof(uv_connect_t), MEM_CAT_JS_RUNTIME);
    creq->data = sock;
    sock->connect_pending = true;
    socket_update_state_properties(sock);
    int r = uv_tcp_connect(creq, &sock->tcp, addr, client_connect_cb);
    if (r != 0) {
        sock->connect_pending = false;
        socket_update_state_properties(sock);
        mem_free(creq);
    }
    return r;
}

static void socket_record_auto_attempt(JsSocket* sock, const char* host, int port) {
    if (!sock || !sock->js_object.item || !host || !host[0]) return;
    Item arr = js_property_get(sock->js_object, make_string_item("autoSelectFamilyAttemptedAddresses"));
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        arr = js_array_new(0);
        js_property_set(sock->js_object, make_string_item("autoSelectFamilyAttemptedAddresses"), arr);
    }
    char endpoint[320];
    snprintf(endpoint, sizeof(endpoint), "%s:%d", host, port);
    js_array_push(arr, make_string_item(endpoint));
}

static bool socket_connect_auto_next(JsSocket* sock) {
    if (!sock || sock->destroyed) return false;
    Item block_list = sock->js_object.item ?
        js_property_get(sock->js_object, make_string_item("__block_list__")) : make_undefined_item();
    while (sock->auto_addr_index < sock->auto_addr_count) {
        int index = sock->auto_addr_index++;
        const char* host = sock->auto_addrs[index];
        int family = sock->auto_families[index];
        if (net_block_list_blocks_item(block_list, host, family)) continue;
        socket_record_auto_attempt(sock, host, sock->connect_port);
        if (family == 6) {
            struct sockaddr_in6 addr6;
            if (uv_ip6_addr(host, sock->connect_port, &addr6) == 0) {
                return socket_connect_resolved(sock, (const struct sockaddr*)&addr6) == 0;
            }
        } else {
            struct sockaddr_in addr4;
            if (uv_ip4_addr(host, sock->connect_port, &addr4) == 0) {
                return socket_connect_resolved(sock, (const struct sockaddr*)&addr4) == 0;
            }
        }
    }
    return false;
}

static void socket_auto_retry_after_close_cb(uv_handle_t* handle) {
    JsSocket* sock = handle ? (JsSocket*)handle->data : NULL;
    uv_loop_t* loop = handle ? handle->loop : lambda_uv_loop();
    if (!sock || sock->destroyed || !loop) return;
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;
    socket_expose_handle(sock);
    uv_ref((uv_handle_t*)&sock->tcp);
    if (!socket_connect_auto_next(sock)) {
        sock->connect_pending = false;
        socket_update_state_properties(sock);
        Item err = make_uv_error(UV_EINVAL, "connect",
            sock->connect_host[0] ? sock->connect_host : NULL,
            sock->connect_host[0] ? sock->connect_port : -1);
        socket_fail_pending_writes(sock, err);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
    }
}

static bool socket_schedule_auto_retry(JsSocket* sock) {
    if (!sock || sock->destroyed) return false;
    if (sock->auto_addr_index >= sock->auto_addr_count) return false;
    sock->connect_pending = true;
    socket_update_state_properties(sock);
    if (uv_is_closing((uv_handle_t*)&sock->tcp)) return true;
    uv_close((uv_handle_t*)&sock->tcp, socket_auto_retry_after_close_cb);
    return true;
}

static int socket_bind_local(JsSocket* sock, const NetConnectOptions* options, int family) {
    if (!sock || !options) return 0;
    if (!options->has_local_address && !options->has_local_port) return 0;

    int port = options->has_local_port ? options->local_port : 0;
    if (family == AF_INET6) {
        const char* address = options->has_local_address ? options->local_address : "::";
        struct sockaddr_in6 local6;
        int r = uv_ip6_addr(address, port, &local6);
        if (r != 0) return r;
        return uv_tcp_bind(&sock->tcp, (const struct sockaddr*)&local6, 0);
    }

    const char* address = options->has_local_address ? options->local_address : "0.0.0.0";
    struct sockaddr_in local4;
    int r = uv_ip4_addr(address, port, &local4);
    if (r != 0) return r;
    return uv_tcp_bind(&sock->tcp, (const struct sockaddr*)&local4, 0);
}

static struct addrinfo* net_select_addrinfo(struct addrinfo* res, int family) {
    if (!res) return NULL;
    int preferred = AF_INET;
    if (family == 6) preferred = AF_INET6;
    if (family == 4) preferred = AF_INET;

    for (struct addrinfo* cur = res; cur; cur = cur->ai_next) {
        if (cur->ai_family == preferred) return cur;
    }
    return res;
}

static bool net_copy_lookup_address(Item value, char* out, int out_size, int* out_family) {
    if (!out || out_size <= 0) return false;
    Item address = value;
    Item family = make_undefined_item();
    if (get_type_id(value) == LMD_TYPE_MAP) {
        address = js_property_get(value, make_string_item("address"));
        family = js_property_get(value, make_string_item("family"));
    }
    if (!copy_string_item(address, out, out_size)) return false;
    if (out_family) {
        int detected = 0;
        if (get_type_id(family) == LMD_TYPE_INT) detected = (int)it2i(family);
        if (detected != 4 && detected != 6) {
            struct sockaddr_in addr4;
            struct sockaddr_in6 addr6;
            if (uv_ip4_addr(out, 0, &addr4) == 0) detected = 4;
            else if (uv_ip6_addr(out, 0, &addr6) == 0) detected = 6;
        }
        *out_family = detected == 6 ? 6 : 4;
    }
    return true;
}

static void net_connect_lookup_fail(NetResolveReq* nr, Item err) {
    if (!nr || !nr->sock) return;
    JsSocket* sock = nr->sock;
    sock->connect_pending = false;
    socket_update_state_properties(sock);
    socket_fail_pending_writes(sock, err);
    Item lookup_args[4] = { err, make_undefined_item(), make_undefined_item(), make_string_item(nr->host) };
    socket_emit(sock->js_object, "lookup", lookup_args, 4);
    socket_emit(sock->js_object, "error", &err, 1);
    socket_close_now(sock);
}

static Item net_lookup_complete(Item env_item, Item rest_args) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    NetResolveReq* nr = (NetResolveReq*)(uintptr_t)it2i(env[0]);
    if (!nr) return make_undefined_item();

    JsSocket* sock = nr->sock;
    if (!sock || sock->destroyed) {
        mem_free(nr);
        return make_undefined_item();
    }

    int64_t argc = js_array_length(rest_args);
    Item err = argc > 0 ? js_array_get_int(rest_args, 0) : make_undefined_item();
    if (!is_undefined_item(err) && err.item != ITEM_NULL) {
        net_connect_lookup_fail(nr, err);
        mem_free(nr);
        return make_undefined_item();
    }

    Item value = argc > 1 ? js_array_get_int(rest_args, 1) : make_undefined_item();
    Item family_item = argc > 2 ? js_array_get_int(rest_args, 2) : make_undefined_item();
    char first_addr[INET6_ADDRSTRLEN];
    first_addr[0] = '\0';
    int first_family = get_type_id(family_item) == LMD_TYPE_INT ? (int)it2i(family_item) : 0;

    if (nr->auto_select_family && get_type_id(value) == LMD_TYPE_ARRAY) {
        int64_t len64 = js_array_length(value);
        int len = len64 > 8 ? 8 : (int)len64;
        sock->auto_addr_count = 0;
        sock->auto_addr_index = 0;
        sock->connect_port = nr->port;
        for (int i = 0; i < len; i++) {
            Item record = js_array_get_int(value, i);
            int family = 0;
            if (net_copy_lookup_address(record, sock->auto_addrs[sock->auto_addr_count],
                    (int)sizeof(sock->auto_addrs[0]), &family)) {
                sock->auto_families[sock->auto_addr_count] = family;
                sock->auto_addr_count++;
            }
        }
        if (sock->auto_addr_count > 0 && socket_connect_auto_next(sock)) {
            mem_free(nr);
            return make_undefined_item();
        }
        if (nr->has_block_list) {
            for (int i = 0; i < sock->auto_addr_count; i++) {
                if (net_block_list_blocks_item(nr->block_list, sock->auto_addrs[i], sock->auto_families[i])) {
                    socket_fail_ip_blocked(sock, sock->auto_addrs[i]);
                    mem_free(nr);
                    return make_undefined_item();
                }
            }
        }
    } else if (net_copy_lookup_address(value, first_addr, (int)sizeof(first_addr), &first_family)) {
        Item lookup_args[4] = {
            ItemNull,
            make_string_item(first_addr),
            (Item){.item = i2it(first_family)},
            make_string_item(nr->host)
        };
        socket_emit(sock->js_object, "lookup", lookup_args, 4);

        if (nr->has_block_list && net_block_list_blocks_item(nr->block_list, first_addr, first_family)) {
            socket_fail_ip_blocked(sock, first_addr);
            mem_free(nr);
            return make_undefined_item();
        }

        int r = 0;
        if (first_family == 6) {
            struct sockaddr_in6 addr6;
            r = uv_ip6_addr(first_addr, nr->port, &addr6);
            if (r == 0) r = socket_connect_resolved(sock, (const struct sockaddr*)&addr6);
        } else {
            struct sockaddr_in addr4;
            r = uv_ip4_addr(first_addr, nr->port, &addr4);
            if (r == 0) r = socket_connect_resolved(sock, (const struct sockaddr*)&addr4);
        }
        if (r == 0) {
            mem_free(nr);
            return make_undefined_item();
        }
        Item connect_err = make_uv_error(r, "connect", first_addr, nr->port);
        net_connect_lookup_fail(nr, connect_err);
        mem_free(nr);
        return make_undefined_item();
    }

    Item lookup_err = make_uv_error(UV_EAI_NONAME, "getaddrinfo", nr->host, -1);
    net_connect_lookup_fail(nr, lookup_err);
    mem_free(nr);
    return make_undefined_item();
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
        socket_update_state_properties(sock);
        if (res) uv_freeaddrinfo(res);
        if (uv_is_closing((uv_handle_t*)&sock->tcp)) {
            socket_release_after_pending_connect(sock);
        } else {
            socket_close_now(sock);
        }
        mem_free(nr);
        return;
    }

    if (status != 0 || !res) {
        sock->connect_pending = false;
        socket_update_state_properties(sock);
        Item err = make_uv_error(status, "getaddrinfo", nr->host, -1);
        socket_fail_pending_writes(sock, err);
        Item lookup_args[4] = { err, make_undefined_item(), make_undefined_item(), make_string_item(nr->host) };
        socket_emit(sock->js_object, "lookup", lookup_args, 4);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
        if (res) uv_freeaddrinfo(res);
        mem_free(nr);
        return;
    }

    char addr_str[INET6_ADDRSTRLEN];
    int family = 0;
    addr_str[0] = '\0';
    struct addrinfo* selected = net_select_addrinfo(res, nr->family);
    if (selected->ai_family == AF_INET) {
        struct sockaddr_in* sa = (struct sockaddr_in*)selected->ai_addr;
        uv_ip4_name(sa, addr_str, sizeof(addr_str));
        family = 4;
    } else if (selected->ai_family == AF_INET6) {
        struct sockaddr_in6* sa = (struct sockaddr_in6*)selected->ai_addr;
        uv_ip6_name(sa, addr_str, sizeof(addr_str));
        family = 6;
    }
    if (addr_str[0] != '\0') {
        Item lookup_args[4] = {
            ItemNull,
            make_string_item(addr_str),
            (Item){.item = i2it(family)},
            make_string_item(nr->host)
        };
        socket_emit(sock->js_object, "lookup", lookup_args, 4);
    }

    NetConnectOptions options;
    memset(&options, 0, sizeof(NetConnectOptions));
    options.port = nr->port;
    options.family = nr->family;
    memcpy(options.host, nr->host, sizeof(options.host));
    options.has_local_address = nr->has_local_address;
    options.has_local_port = nr->has_local_port;
    options.local_port = nr->local_port;
    memcpy(options.local_address, nr->local_address, sizeof(options.local_address));
    options.block_list = nr->block_list;
    options.has_block_list = nr->has_block_list;
    if (nr->has_block_list && net_block_list_blocks_item(nr->block_list, addr_str, family)) {
        socket_fail_ip_blocked(sock, addr_str);
        uv_freeaddrinfo(res);
        mem_free(nr);
        return;
    }
    int bind_r = socket_bind_local(sock, &options, selected->ai_family);
    if (bind_r != 0) {
        sock->connect_pending = false;
        socket_update_state_properties(sock);
        Item err = make_uv_error(bind_r, "bind", nr->host, nr->port);
        socket_fail_pending_writes(sock, err);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
        uv_freeaddrinfo(res);
        mem_free(nr);
        return;
    }

    int r = socket_connect_resolved(sock, selected->ai_addr);
    if (r != 0) {
        Item err = make_uv_error(r, "connect", nr->host, nr->port);
        socket_fail_pending_writes(sock, err);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
    }

    uv_freeaddrinfo(res);
    mem_free(nr);
}

static int socket_start_connect(JsSocket* sock, const NetConnectOptions* options) {
    if (!sock || sock->destroyed) return 0;
    socket_expose_handle(sock);
    uv_ref((uv_handle_t*)&sock->tcp);
    bool auto_select_family = options->auto_select_family_set ?
        options->auto_select_family : net_default_auto_select_family;

    if (options->has_lookup) {
        uv_loop_t* loop = lambda_uv_loop();
        if (!loop) return UV_EINVAL;

        NetResolveReq* nr = (NetResolveReq*)mem_calloc(1, sizeof(NetResolveReq), MEM_CAT_JS_RUNTIME);
        nr->sock = sock;
        nr->port = options->port;
        nr->family = options->family;
        memcpy(nr->host, options->host, sizeof(nr->host));
        nr->has_local_address = options->has_local_address;
        nr->has_local_port = options->has_local_port;
        nr->local_port = options->local_port;
        memcpy(nr->local_address, options->local_address, sizeof(nr->local_address));
        nr->auto_select_family = auto_select_family;
        nr->lookup = options->lookup;
        nr->block_list = options->block_list;
        nr->has_block_list = options->has_block_list;

        sock->connect_pending = true;
        sock->auto_select_family = auto_select_family;
        socket_update_state_properties(sock);

        Item lookup_options = js_new_object();
        if (options->family == 4 || options->family == 6) {
            js_property_set(lookup_options, make_string_item("family"),
                            (Item){.item = i2it(options->family)});
        }
        if (auto_select_family) {
            js_property_set(lookup_options, make_string_item("all"), (Item){.item = ITEM_TRUE});
        }
        Item* env = js_alloc_env(1);
        env[0] = (Item){.item = i2it((int64_t)(uintptr_t)nr)};
        Item callback = js_new_closure((void*)net_lookup_complete, -1, env, 1);
        Item args[3] = { make_string_item(options->host), lookup_options, callback };
        js_call_function(options->lookup, make_undefined_item(), args, 3);
        if (js_check_exception()) {
            Item err = js_clear_exception();
            net_connect_lookup_fail(nr, err);
            mem_free(nr);
            return 0;
        }
        js_microtask_flush();
        return 0;
    }

    struct sockaddr_in addr4;
    if ((options->family == 0 || options->family == 4) &&
        uv_ip4_addr(options->host, options->port, &addr4) == 0) {
        if (options->has_block_list && net_block_list_blocks_item(options->block_list, options->host, 4)) {
            socket_fail_ip_blocked(sock, options->host);
            return 0;
        }
        int bind_r = socket_bind_local(sock, options, AF_INET);
        if (bind_r != 0) return bind_r;
        return socket_connect_resolved(sock, (const struct sockaddr*)&addr4);
    }

    struct sockaddr_in6 addr6;
    if ((options->family == 0 || options->family == 6) &&
        uv_ip6_addr(options->host, options->port, &addr6) == 0) {
        if (options->has_block_list && net_block_list_blocks_item(options->block_list, options->host, 6)) {
            socket_fail_ip_blocked(sock, options->host);
            return 0;
        }
        int bind_r = socket_bind_local(sock, options, AF_INET6);
        if (bind_r != 0) return bind_r;
        return socket_connect_resolved(sock, (const struct sockaddr*)&addr6);
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return UV_EINVAL;

    NetResolveReq* nr = (NetResolveReq*)mem_calloc(1, sizeof(NetResolveReq), MEM_CAT_JS_RUNTIME);
    nr->sock = sock;
    nr->port = options->port;
    nr->family = options->family;
    memcpy(nr->host, options->host, sizeof(nr->host));
    nr->has_local_address = options->has_local_address;
    nr->has_local_port = options->has_local_port;
    nr->local_port = options->local_port;
    memcpy(nr->local_address, options->local_address, sizeof(nr->local_address));
    nr->auto_select_family = auto_select_family;
    nr->block_list = options->block_list;
    nr->has_block_list = options->has_block_list;
    snprintf(nr->service, sizeof(nr->service), "%d", options->port);
    nr->req.data = nr;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = options->family == 4 ? AF_INET : (options->family == 6 ? AF_INET6 : AF_UNSPEC);

    sock->connect_pending = true;
    socket_update_state_properties(sock);
    int r = uv_getaddrinfo(loop, &nr->req, net_resolve_cb, nr->host, nr->service, &hints);
    if (r != 0) {
        sock->connect_pending = false;
        socket_update_state_properties(sock);
        mem_free(nr);
    }
    return r;
}

static void socket_store_connect_options(JsSocket* sock, const NetConnectOptions* options) {
    if (!sock || !options) return;
    sock->keep_alive_requested = options->keep_alive;
    sock->no_delay_requested = options->no_delay;
    sock->keep_alive_delay_secs = options->keep_alive_delay_secs;
    if (sock->js_object.item && options->has_block_list) {
        js_property_set(sock->js_object, make_string_item("__block_list__"), options->block_list);
    }
}

static void socket_apply_connect_options(JsSocket* sock) {
    if (!sock || !sock->js_object.item) return;
    Item handle = js_property_get(sock->js_object, make_string_item("_handle"));
    if (handle.item == 0 || handle.item == ITEM_NULL || is_undefined_item(handle)) return;

    if (sock->keep_alive_requested) {
        Item fn = js_property_get(handle, make_string_item("setKeepAlive"));
        if (is_callable(fn)) {
            Item args[2] = {
                (Item){.item = ITEM_TRUE},
                (Item){.item = i2it(sock->keep_alive_delay_secs)}
            };
            js_call_function(fn, handle, args, 2);
            js_microtask_flush();
        }
    }
    if (sock->no_delay_requested) {
        Item fn = js_property_get(handle, make_string_item("setNoDelay"));
        if (is_callable(fn)) {
            Item arg = (Item){.item = ITEM_TRUE};
            js_call_function(fn, handle, &arg, 1);
            js_microtask_flush();
        }
    }
}

static void client_connect_cb(uv_connect_t* req, int status) {
    JsSocket* sock = (JsSocket*)req->data;
    mem_free(req);

    if (!sock) return;
    sock->connect_pending = false;
    socket_update_state_properties(sock);
    if (sock->destroyed) {
        if (uv_is_closing((uv_handle_t*)&sock->tcp)) {
            socket_release_after_pending_connect(sock);
        } else {
            socket_close_now(sock);
        }
        return;
    }

    if (status != 0) {
        if (socket_schedule_auto_retry(sock)) return;
        const char* err_host = sock->connect_host[0] ? sock->connect_host : NULL;
        int err_port = err_host ? sock->connect_port : -1;
        Item err = make_uv_error(status, "connect", err_host, err_port);
        socket_fail_pending_writes(sock, err);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
        return;
    }

    sock->connected = true;
    socket_update_state_properties(sock);
    socket_update_address_properties(sock);
    socket_apply_connect_options(sock);
    socket_apply_type_of_service(sock);
    socket_flush_pending_writes(sock);
    socket_emit(sock->js_object, "connect", NULL, 0);
    socket_emit(sock->js_object, "ready", NULL, 0);
    if (sock->end_after_connect) {
        Item callback = sock->end_callback_set ? sock->end_callback : make_undefined_item();
        sock->end_callback_set = false;
        sock->end_callback = make_undefined_item();
        socket_shutdown_writes(sock, callback);
    }

    if (!sock->paused) {
        socket_start_read(sock);
    }
}

static Item create_socket_for_connect(const NetConnectOptions* options) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("net: createConnection: no event loop");
        return ItemNull;
    }

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    sock->high_water_mark = 16 * 1024;
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    Item obj = make_socket_object(sock, true);
    if (options->allow_half_open) {
        js_property_set(obj, make_string_item("allowHalfOpen"), (Item){.item = ITEM_TRUE});
    }
    socket_store_connect_options(sock, options);
    if (options->has_onread) socket_configure_onread(sock, options->onread);
    if (options->has_signal && socket_configure_abort_signal(sock, options->signal)) return obj;
    if (is_callable(options->callback)) socket_set_listener(obj, "connect", options->callback);

    int r = socket_start_connect(sock, options);
    if (r != 0) {
        Item err = make_uv_error(r, "connect", options->host, options->port);
        socket_fail_pending_writes(sock, err);
        socket_emit(obj, "error", &err, 1);
        socket_close_now(sock);
    }
    return obj;
}

static JsSocket* socket_reattach_for_connect(Item self) {
    TypeId self_type = get_type_id(self);
    if (self_type != LMD_TYPE_MAP && self_type != LMD_TYPE_OBJECT && self_type != LMD_TYPE_VMAP) {
        return NULL;
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return NULL;

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    sock->high_water_mark = 16 * 1024;
    sock->js_object = self;
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    js_property_set(self, make_string_item("__handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)sock)});
    js_property_set(self, make_string_item("_handle"), make_socket_handle_object(sock));
    sock->handle_exposed = true;
    js_property_set(self, make_string_item("destroyed"), (Item){.item = ITEM_FALSE});
    js_property_set(self, make_string_item("readable"), (Item){.item = ITEM_TRUE});
    js_property_set(self, make_string_item("writable"), (Item){.item = ITEM_TRUE});
    socket_update_io_counters(sock);
    socket_update_state_properties(sock);
    net_active_add(net_active_sockets, NET_ACTIVE_SOCKET_MAX, self);
    return sock;
}

static Item js_socket_connect_args(Item self, Item rest_args) {
    NetConnectOptions options;
    if (!normalize_connect_args(rest_args, &options)) return ItemNull;

    JsSocket* sock = socket_from_object(self);
    if (!sock) sock = socket_reattach_for_connect(self);
    if (!sock || sock->destroyed) return self;
    if (options.allow_half_open) {
        js_property_set(self, make_string_item("allowHalfOpen"), (Item){.item = ITEM_TRUE});
    }
    socket_store_connect_options(sock, &options);
    if (options.has_onread) socket_configure_onread(sock, options.onread);
    if (options.has_signal && socket_configure_abort_signal(sock, options.signal)) return self;
    if (is_callable(options.callback)) socket_set_listener(self, "connect", options.callback);

    int r = socket_start_connect(sock, &options);
    if (r != 0) {
        Item err = make_uv_error(r, "connect", options.host, options.port);
        socket_fail_pending_writes(sock, err);
        socket_emit(self, "error", &err, 1);
        socket_close_now(sock);
    }
    return self;
}

extern "C" Item js_net_createConnection(Item rest_args) {
    Item connect_fn = ItemNull;
    if (get_type_id(net_socket_prototype) == LMD_TYPE_MAP) {
        connect_fn = js_property_get(net_socket_prototype, make_string_item("connect"));
    }
    bool patched_connect = is_callable(connect_fn) &&
        net_socket_connect_fn.item != 0 &&
        connect_fn.item != net_socket_connect_fn.item;
    if (!patched_connect) {
        NetConnectOptions options;
        if (!normalize_connect_args(rest_args, &options)) return ItemNull;
        return create_socket_for_connect(&options);
    }

    Item socket = js_net_Socket(make_undefined_item());

    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 16 ? 16 : (int)argc64;
    Item args[16];
    for (int i = 0; i < argc; i++) {
        args[i] = js_array_get_int(rest_args, i);
    }
    js_call_function(connect_fn, socket, args, argc);
    js_microtask_flush();
    return socket;
}

// =============================================================================
// net.createServer(connectionHandler) — TCP server
// =============================================================================

struct JsServer {
    uv_tcp_t tcp;
    Item     js_object;
    Item     connection_handler;
    bool     closed;
    bool     listen_pending;
    bool     allow_half_open;
    bool     close_requested;
    bool     handle_closed;
    bool     close_event_emitted;
    bool     has_block_list;
    int      connection_count;
    Item     block_list;
};

static void server_maybe_finish_close(JsServer* srv) {
    if (!srv || !srv->close_requested || !srv->handle_closed) return;
    if (srv->connection_count > 0) return;

    // the listening handle can close before accepted sockets finish closing.
    // keep JsServer alive until all owner_server links are detached, or socket
    // close callbacks can decrement connection_count through a freed pointer.
    net_active_remove(net_active_servers, NET_ACTIVE_SERVER_MAX, srv->js_object);
    if (!srv->close_event_emitted) {
        srv->close_event_emitted = true;
        Item on_close = js_property_get(srv->js_object, make_string_item("__on_close__"));
        if (is_callable(on_close)) {
            js_call_function(on_close, srv->js_object, NULL, 0);
        }
    }
    mem_free(srv);
}

static void socket_note_closed(JsSocket* sock) {
    if (!sock || !sock->owner_server) return;
    JsServer* srv = sock->owner_server;
    if (srv->connection_count > 0) {
        srv->connection_count--;
    }
    sock->owner_server = NULL;
    server_maybe_finish_close(srv);
}

static void server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

static void server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    JsSocket* sock = (JsSocket*)stream->data;
    if (nread > 0 && sock) {
        sock->bytes_read += (int64_t)nread;
        socket_update_io_counters(sock);
        socket_emit_read_data(sock, buf->base, (int)nread);
    }
    if (buf->base) mem_free(buf->base);
    if (nread < 0 && sock && !sock->destroyed) {
        sock->reading = false;
        if (nread != UV_EOF) {
            Item err = make_uv_error((int)nread, "read", NULL, -1);
            socket_emit(sock->js_object, "error", &err, 1);
            if (sock->destroyed) return;
        }
        socket_handle_remote_eof(sock);
    }
}

static int server_max_connections(JsServer* srv) {
    if (!srv || !srv->js_object.item) return -1;
    Item max_item = js_property_get(srv->js_object, make_string_item("maxConnections"));
    if (get_type_id(max_item) == LMD_TYPE_INT) return (int)it2i(max_item);
    if (get_type_id(max_item) == LMD_TYPE_INT64) return (int)it2l(max_item);
    return -1;
}

static void drop_set_endpoint(Item obj, const char* prefix, const struct sockaddr_storage* addr) {
    if (!obj.item || !addr) return;
    char address[INET6_ADDRSTRLEN];
    const char* family = NULL;
    int port = 0;
    address[0] = '\0';
    if (addr->ss_family == AF_INET) {
        const struct sockaddr_in* a4 = (const struct sockaddr_in*)addr;
        uv_ip4_name(a4, address, sizeof(address));
        family = "IPv4";
        port = ntohs(a4->sin_port);
    } else if (addr->ss_family == AF_INET6) {
        const struct sockaddr_in6* a6 = (const struct sockaddr_in6*)addr;
        uv_ip6_name(a6, address, sizeof(address));
        family = "IPv6";
        port = ntohs(a6->sin6_port);
    } else {
        return;
    }

    char key[32];
    snprintf(key, sizeof(key), "%sAddress", prefix);
    js_property_set(obj, make_string_item(key), make_string_item(address));
    snprintf(key, sizeof(key), "%sPort", prefix);
    js_property_set(obj, make_string_item(key), (Item){.item = i2it(port)});
    snprintf(key, sizeof(key), "%sFamily", prefix);
    js_property_set(obj, make_string_item(key), make_string_item(family));
}

static Item server_make_drop_data(JsSocket* client) {
    Item data = js_new_object();
    if (!client) return data;

    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    if (uv_tcp_getsockname(&client->tcp, (struct sockaddr*)&addr, &addrlen) == 0) {
        drop_set_endpoint(data, "local", &addr);
    }
    addrlen = sizeof(addr);
    if (uv_tcp_getpeername(&client->tcp, (struct sockaddr*)&addr, &addrlen) == 0) {
        drop_set_endpoint(data, "remote", &addr);
    }
    return data;
}

static void server_emit_drop(JsServer* srv, JsSocket* client) {
    if (!srv || !srv->js_object.item) return;
    Item on_drop = js_property_get(srv->js_object, make_string_item("__on_drop__"));
    if (!is_callable(on_drop)) return;
    Item data = server_make_drop_data(client);
    js_call_function(on_drop, srv->js_object, &data, 1);
    js_microtask_flush();
}

static bool net_capture_rejections_enabled(void) {
    Item events = js_module_get(make_string_item("events"));
    if (events.item == 0 || events.item == ITEM_NULL || is_undefined_item(events)) return false;
    Item value = js_property_get(events, make_string_item("captureRejections"));
    return get_type_id(value) == LMD_TYPE_BOOL && it2b(value);
}

static Item server_connection_rejection(Item env_item, Item reason) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item client_obj = env[0];
    JsSocket* sock = socket_from_object(client_obj);
    if (!sock || sock->destroyed) return make_undefined_item();
    socket_emit(client_obj, "error", &reason, 1);
    if (!sock->destroyed) socket_close_now(sock);
    return make_undefined_item();
}

static void server_capture_connection_rejection(Item result, Item client_obj) {
    if (!net_capture_rejections_enabled()) return;
    if (get_type_id(result) != LMD_TYPE_MAP || js_class_id(result) != JS_CLASS_PROMISE) return;
    Item* env = js_alloc_env(1);
    env[0] = client_obj;
    Item reject = js_new_closure((void*)server_connection_rejection, 1, env, 1);
    js_promise_then(result, make_undefined_item(), reject);
}

static void server_connection_cb(uv_stream_t* server, int status) {
    if (status < 0) return;

    JsServer* srv = (JsServer*)server->data;
    if (!srv) return;

    uv_loop_t* loop = server->loop;

    JsSocket* client = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    client->high_water_mark = 16 * 1024;
    uv_tcp_init(loop, &client->tcp);
    client->tcp.data = client;

    if (uv_accept(server, (uv_stream_t*)&client->tcp) == 0) {
        if (!context || !context->heap) {
            uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
                mem_free(h->data);
            });
            return;
        }

        if (srv->has_block_list) {
            struct sockaddr_storage peer_addr;
            int peer_len = sizeof(peer_addr);
            char peer_ip[INET6_ADDRSTRLEN];
            int peer_family = 0;
            peer_ip[0] = '\0';
            if (uv_tcp_getpeername(&client->tcp, (struct sockaddr*)&peer_addr, &peer_len) == 0) {
                if (peer_addr.ss_family == AF_INET) {
                    uv_ip4_name((const struct sockaddr_in*)&peer_addr, peer_ip, sizeof(peer_ip));
                    peer_family = 4;
                } else if (peer_addr.ss_family == AF_INET6) {
                    uv_ip6_name((const struct sockaddr_in6*)&peer_addr, peer_ip, sizeof(peer_ip));
                    peer_family = 6;
                }
            }
            if (peer_ip[0] && net_block_list_blocks_item(srv->block_list, peer_ip, peer_family)) {
                uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
                    mem_free(h->data);
                });
                return;
            }
        }

        int max_connections = server_max_connections(srv);
        if (max_connections >= 0 && srv->connection_count >= max_connections) {
            server_emit_drop(srv, client);
            uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
                mem_free(h->data);
            });
            return;
        }

        Item client_obj = make_socket_object(client, true);
        client->connected = true;
        client->is_server_side = true;
        client->owner_server = srv;
        js_property_set(client_obj, make_string_item("allowHalfOpen"),
                        (Item){.item = b2it(srv->allow_half_open)});
        srv->connection_count++;
        socket_update_state_properties(client);
        socket_update_address_properties(client);

        // call connection handler
        if (get_type_id(srv->connection_handler) == LMD_TYPE_FUNC) {
            Item result = js_call_function(srv->connection_handler, srv->js_object, &client_obj, 1);
            server_capture_connection_rejection(result, client_obj);
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

static Item js_server_emit_error_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    Item err = env[1];
    Item on_error = js_property_get(self, make_string_item("__on_error__"));
    if (is_callable(on_error)) {
        js_call_function(on_error, self, &err, 1);
        js_microtask_flush();
    }
    return make_undefined_item();
}

static void server_schedule_error(Item self, Item err) {
    Item* env = js_alloc_env(2);
    env[0] = self;
    env[1] = err;
    Item fn = js_new_closure((void*)js_server_emit_error_scheduled, 0, env, 2);
    js_next_tick_enqueue(fn);
}

static void server_update_connection_key(Item self, JsServer* srv, int requested_port) {
    if (!srv || !self.item) return;

    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    int r = uv_tcp_getsockname(&srv->tcp, (struct sockaddr*)&addr, &addrlen);
    if (r != 0) return;

    char address[INET6_ADDRSTRLEN];
    char family_digit = '4';
    address[0] = '\0';
    if (addr.ss_family == AF_INET) {
        uv_ip4_name((const struct sockaddr_in*)&addr, address, sizeof(address));
        family_digit = '4';
    } else if (addr.ss_family == AF_INET6) {
        uv_ip6_name((const struct sockaddr_in6*)&addr, address, sizeof(address));
        family_digit = '6';
    } else {
        return;
    }

    char key[320];
    snprintf(key, sizeof(key), "%c:%s:%d", family_digit, address, requested_port);
    js_property_set(self, make_string_item("_connectionKey"), make_string_item(key));
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
    if (is_callable(port_item)) {
        callback = port_item;
        port_item = (Item){.item = i2it(0)};
        host_item = make_undefined_item();
    } else if (is_undefined_item(port_item) || port_item.item == ITEM_NULL) {
        port_item = (Item){.item = i2it(0)};
    }

    int port = 0;
    char host_buf[256] = "0.0.0.0";
    bool ipv6_only = false;
    bool reuse_port = false;

    TypeId port_type = get_type_id(port_item);
    if (port_type == LMD_TYPE_MAP || port_type == LMD_TYPE_OBJECT || port_type == LMD_TYPE_VMAP) {
        Item opt_port = js_property_get(port_item, make_string_item("port"));
        if (!parse_port(opt_port, &port)) return self;
        Item opt_host = js_property_get(port_item, make_string_item("host"));
        if (!is_undefined_item(opt_host) && opt_host.item != ITEM_NULL) {
            if (!copy_string_item(opt_host, host_buf, (int)sizeof(host_buf))) {
                js_throw_invalid_arg_type("options.host", "string", opt_host);
                return self;
            }
        }
        Item opt_ipv6_only = js_property_get(port_item, make_string_item("ipv6Only"));
        ipv6_only = get_type_id(opt_ipv6_only) == LMD_TYPE_BOOL && it2b(opt_ipv6_only);
        Item opt_reuse_port = js_property_get(port_item, make_string_item("reusePort"));
        reuse_port = get_type_id(opt_reuse_port) == LMD_TYPE_BOOL && it2b(opt_reuse_port);
    } else if (port_type == LMD_TYPE_STRING) {
        String* s = it2s(port_item);
        char first[256];
        int len = (int)s->len < 255 ? (int)s->len : 255;
        memcpy(first, s->chars, (size_t)len);
        first[len] = '\0';

        char* end = NULL;
        long parsed = strtol(first, &end, 0);
        if (end == first || *end != '\0' || parsed < 0 || parsed > 65535) {
            Item err = make_uv_error(UV_EACCES, "listen", first, -1);
            server_schedule_error(self, err);
            return self;
        }
        port = (int)parsed;
    } else {
        if (!parse_port(port_item, &port)) return self;
    }

    if (get_type_id(host_item) == LMD_TYPE_STRING) {
        String* h = it2s(host_item);
        int len = (int)h->len < 255 ? (int)h->len : 255;
        memcpy(host_buf, h->chars, (size_t)len);
        host_buf[len] = '\0';
    }

    struct sockaddr_storage addr;
    int flags = 0;
#ifdef UV_TCP_REUSEPORT
    if (reuse_port) flags |= UV_TCP_REUSEPORT;
#else
    if (reuse_port) {
        Item err = make_uv_error(UV_ENOSYS, "listen", host_buf, port);
        server_schedule_error(self, err);
        return self;
    }
#endif
    int r = uv_ip4_addr(host_buf, port, (struct sockaddr_in*)&addr);
    if (r != 0) {
        r = uv_ip6_addr(host_buf, port, (struct sockaddr_in6*)&addr);
        if (r == 0 && ipv6_only) flags = UV_TCP_IPV6ONLY;
    }
    if (r != 0) {
        Item err = make_uv_error(r, "listen", host_buf, port);
        server_schedule_error(self, err);
        return self;
    }

    r = uv_tcp_bind(&srv->tcp, (const struct sockaddr*)&addr, (unsigned int)flags);
    if (r != 0) {
        srv->listen_pending = false;
        Item err = make_uv_error(r, "listen", host_buf, port);
        server_schedule_error(self, err);
        return self;
    }
    server_update_connection_key(self, srv, port);

    r = uv_listen((uv_stream_t*)&srv->tcp, 128, server_connection_cb);
    if (r != 0) {
        srv->listen_pending = false;
        Item err = make_uv_error(r, "listen", host_buf, port);
        server_schedule_error(self, err);
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

static JsServer* server_from_object(Item self) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (JsServer*)(uintptr_t)it2i(handle_item);
}

// server.ref() / server.unref()
static Item js_server_ref(void) {
    Item self = js_get_this();
    JsServer* srv = server_from_object(self);
    if (srv && !uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_ref((uv_handle_t*)&srv->tcp);
    }
    return self;
}

static Item js_server_unref(void) {
    Item self = js_get_this();
    JsServer* srv = server_from_object(self);
    if (srv && !uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_unref((uv_handle_t*)&srv->tcp);
    }
    return self;
}

// server.getConnections(callback) — stub: always reports 0
static Item js_server_getConnections(Item callback) {
    Item self = js_get_this();
    JsServer* srv = server_from_object(self);
    int connections = srv ? srv->connection_count : 0;
    if (is_callable(callback)) {
        Item args[2] = { ItemNull, (Item){.item = i2it(connections)} };
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
    srv->close_requested = true;
    srv->listen_pending = false;
    if (is_callable(callback)) {
        js_property_set(self, make_string_item("__on_close__"), callback);
    }
    js_property_set(self, make_string_item("__server__"), ItemNull);

    if (!uv_is_closing((uv_handle_t*)&srv->tcp)) {
        uv_close((uv_handle_t*)&srv->tcp, [](uv_handle_t* h) {
            JsServer* s = (JsServer*)h->data;
            if (s) {
                s->handle_closed = true;
                server_maybe_finish_close(s);
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

extern "C" Item js_net_createServer(Item rest_args) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("net: createServer: no event loop");
        return ItemNull;
    }

    Item options = make_undefined_item();
    Item handler = make_undefined_item();
    int64_t argc = js_array_length(rest_args);
    if (argc > 0) {
        Item first = js_array_get_int(rest_args, 0);
        if (is_callable(first)) {
            handler = first;
        } else {
            options = first;
            if (argc > 1) {
                Item second = js_array_get_int(rest_args, 1);
                if (is_callable(second)) handler = second;
            }
        }
    } else if (is_callable(rest_args)) {
        handler = rest_args;
    }

    JsServer* srv = (JsServer*)mem_calloc(1, sizeof(JsServer), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &srv->tcp);
    srv->tcp.data = srv;
    srv->connection_handler = handler;
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT ||
        get_type_id(options) == LMD_TYPE_VMAP) {
        Item allow_half_open = js_property_get(options, make_string_item("allowHalfOpen"));
        srv->allow_half_open = get_type_id(allow_half_open) == LMD_TYPE_BOOL && it2b(allow_half_open);
        Item block_list = js_property_get(options, make_string_item("blockList"));
        if (!is_undefined_item(block_list) && block_list.item != ITEM_NULL &&
            net_block_list_from_item(block_list) != NULL) {
            srv->block_list = block_list;
            srv->has_block_list = true;
        }
    }

    Item obj = js_new_object();
    // T5b: legacy `__class_name__` string write retired.
    js_class_stamp(obj, JS_CLASS_SERVER);  // A3-T3b
    if (get_type_id(net_server_prototype) == LMD_TYPE_MAP) {
        js_set_prototype(obj, net_server_prototype);
    }
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
    js_property_set(obj, make_string_item("allowHalfOpen"),
                    (Item){.item = b2it(srv->allow_half_open)});
    if (srv->has_block_list) {
        js_property_set(obj, make_string_item("__block_list__"), srv->block_list);
    }

    srv->js_object = obj;
    net_active_add(net_active_servers, NET_ACTIVE_SERVER_MAX, obj);
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

extern "C" Item js_net_Socket(Item options) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return ItemNull;

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    sock->high_water_mark = 16 * 1024;
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT ||
        get_type_id(options) == LMD_TYPE_VMAP) {
        Item hwm = js_property_get(options, make_string_item("highWaterMark"));
        if (get_type_id(hwm) == LMD_TYPE_INT && it2i(hwm) >= 0) {
            sock->high_water_mark = it2i(hwm);
        }
    }
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    uv_unref((uv_handle_t*)&sock->tcp);
    Item obj = make_socket_object(sock, false);
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT ||
        get_type_id(options) == LMD_TYPE_VMAP) {
        Item handle = js_property_get(options, make_string_item("handle"));
        if (get_type_id(handle) == LMD_TYPE_MAP || get_type_id(handle) == LMD_TYPE_OBJECT ||
            get_type_id(handle) == LMD_TYPE_VMAP) {
            js_property_set(obj, make_string_item("_handle"), handle);
            sock->handle_exposed = true;
        }
        Item readable = js_property_get(options, make_string_item("readable"));
        if (get_type_id(readable) == LMD_TYPE_BOOL) {
            js_property_set(obj, make_string_item("readable"), readable);
        }
        Item writable = js_property_get(options, make_string_item("writable"));
        if (get_type_id(writable) == LMD_TYPE_BOOL) {
            js_property_set(obj, make_string_item("writable"), writable);
        }
        Item signal = js_property_get(options, make_string_item("signal"));
        if (!is_undefined_item(signal) && signal.item != ITEM_NULL) {
            socket_configure_abort_signal(sock, signal);
        }
        Item onread = js_property_get(options, make_string_item("onread"));
        if (!is_undefined_item(onread) && onread.item != ITEM_NULL) {
            socket_configure_onread(sock, onread);
        }
    }
    return obj;
}

static Item js_block_list_addAddress(Item address, Item type) {
    Item self = js_get_this();
    NetBlockList* list = net_block_list_from_item(self);
    if (!list) return self;
    if (list->count >= NET_BLOCK_LIST_MAX) return self;

    NetBlockListEntry entry;
    if (!net_block_list_parse_address(address, type, &entry)) {
        js_throw_invalid_arg_type("address", "valid IP address", address);
        return self;
    }
    list->entries[list->count++] = entry;
    return self;
}

static Item js_block_list_addSubnet(Item address, Item prefix, Item type) {
    Item self = js_get_this();
    NetBlockList* list = net_block_list_from_item(self);
    if (!list) return self;
    if (list->count >= NET_BLOCK_LIST_MAX) return self;

    NetBlockListEntry entry;
    if (!net_block_list_parse_address(address, type, &entry)) {
        js_throw_invalid_arg_type("address", "valid IP address", address);
        return self;
    }
    if (!net_block_list_parse_prefix(prefix, entry.family, &entry.prefix)) {
        js_throw_out_of_range("prefix", entry.family == 6 ? ">= 0 && <= 128" : ">= 0 && <= 32", prefix);
        return self;
    }
    list->entries[list->count++] = entry;
    return self;
}

static Item js_block_list_check(Item address, Item type) {
    Item self = js_get_this();
    NetBlockList* list = net_block_list_from_item(self);
    if (!list) return (Item){.item = ITEM_FALSE};
    NetBlockListEntry entry;
    if (!net_block_list_parse_address(address, type, &entry)) return (Item){.item = ITEM_FALSE};
    return (Item){.item = b2it(net_block_list_check_parsed(list, &entry))};
}

static Item js_block_list_isBlockList(Item value) {
    return (Item){.item = b2it(net_block_list_from_item(value) != NULL)};
}

extern "C" Item js_net_BlockList(Item options) {
    (void)options;
    NetBlockList* list = net_block_list_alloc();
    Item obj = js_new_object();
    if (list) {
        js_property_set(obj, make_string_item("__net_block_list__"),
                        (Item){.item = i2it((int64_t)(uintptr_t)list)});
    }
    js_property_set(obj, make_string_item("addAddress"),
                    js_new_function((void*)js_block_list_addAddress, 2));
    js_property_set(obj, make_string_item("addSubnet"),
                    js_new_function((void*)js_block_list_addSubnet, 3));
    js_property_set(obj, make_string_item("check"),
                    js_new_function((void*)js_block_list_check, 2));
    return obj;
}

// =============================================================================
// net Module Namespace
// =============================================================================

static Item net_namespace = {0};

static Item net_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
    return fn;
}

static Item net_constructor_prototype(Item ctor, JsClass cls) {
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

static Item js_net_getDefaultAutoSelectFamilyAttemptTimeout(void) {
    return (Item){.item = i2it(net_auto_select_family_timeout)};
}

static Item js_net_getDefaultAutoSelectFamily(void) {
    return (Item){.item = b2it(net_default_auto_select_family)};
}

static Item js_net_setDefaultAutoSelectFamily(Item enabled_item) {
    net_default_auto_select_family = js_is_truthy(enabled_item);
    return (Item){.item = ITEM_UNDEFINED};
}

static Item js_net_setDefaultAutoSelectFamilyAttemptTimeout(Item timeout_item) {
    if (get_type_id(timeout_item) == LMD_TYPE_INT)
        net_auto_select_family_timeout = (int)it2i(timeout_item);
    return (Item){.item = ITEM_UNDEFINED};
}

static Item js_net_normalizeArgs(Item input) {
    Item result = js_array_new(0);
    Item options = js_new_object();
    Item callback = ItemNull;

    if (get_type_id(input) == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(input);
        if (len > 0) {
            Item first = js_array_get_int(input, 0);
            if (get_type_id(first) == LMD_TYPE_MAP ||
                get_type_id(first) == LMD_TYPE_OBJECT ||
                get_type_id(first) == LMD_TYPE_VMAP) {
                options = first;
            } else if (!is_undefined_item(first) && first.item != ITEM_NULL) {
                js_property_set(options, make_string_item("port"), first);
            }
        }
        if (len > 1) {
            Item second = js_array_get_int(input, 1);
            if (is_callable(second)) {
                callback = second;
            } else if (!is_undefined_item(second) && second.item != ITEM_NULL &&
                       get_type_id(options) == LMD_TYPE_MAP) {
                js_property_set(options, make_string_item("host"), second);
            }
        }
        if (len > 2) {
            Item third = js_array_get_int(input, 2);
            if (is_callable(third)) callback = third;
        }
    }

    js_array_push(result, options);
    js_array_push(result, callback);
    js_property_set(result, net_normalized_args_key(), (Item){.item = ITEM_TRUE});
    return result;
}

extern "C" Item js_get_internal_net_namespace(void) {
    static Item internal_net_namespace = {0};
    if (internal_net_namespace.item != 0) return internal_net_namespace;

    internal_net_namespace = js_new_object();
    heap_register_gc_root(&internal_net_namespace.item);
    js_property_set(internal_net_namespace, make_string_item("normalizedArgsSymbol"),
                    net_normalized_args_key());
    js_property_set(internal_net_namespace, make_string_item("kReinitializeHandle"),
                    make_string_item("kReinitializeHandle"));
    return internal_net_namespace;
}

extern "C" Item js_get_net_namespace(void) {
    if (net_namespace.item != 0) return net_namespace;

    net_namespace = js_new_object();

    Item create_server_fn = net_set_method(net_namespace, "createServer", (void*)js_net_createServer, -1);
    net_set_method(net_namespace, "createConnection", (void*)js_net_createConnection, -1);
    net_set_method(net_namespace, "connect",          (void*)js_net_createConnection, -1); // alias
    Item socket_fn = net_set_method(net_namespace, "Socket", (void*)js_net_Socket, 1);
    Item block_list_fn = net_set_method(net_namespace, "BlockList", (void*)js_net_BlockList, 1);
    Item stream_fn = net_set_method(net_namespace, "Stream", (void*)js_net_Socket, 1); // legacy alias
    Item server_fn = net_set_method(net_namespace, "Server", (void*)js_net_createServer, -1); // alias
    net_set_method(net_namespace, "isIP",             (void*)js_net_isIP, 1);
    net_set_method(net_namespace, "isIPv4",           (void*)js_net_isIPv4, 1);
    net_set_method(net_namespace, "isIPv6",           (void*)js_net_isIPv6, 1);
    net_set_method(net_namespace, "getDefaultAutoSelectFamily",
                   (void*)js_net_getDefaultAutoSelectFamily, 0);
    net_set_method(net_namespace, "setDefaultAutoSelectFamily",
                   (void*)js_net_setDefaultAutoSelectFamily, 1);
    net_set_method(net_namespace, "getDefaultAutoSelectFamilyAttemptTimeout",
                   (void*)js_net_getDefaultAutoSelectFamilyAttemptTimeout, 0);
    net_set_method(net_namespace, "setDefaultAutoSelectFamilyAttemptTimeout",
                   (void*)js_net_setDefaultAutoSelectFamilyAttemptTimeout, 1);
    net_set_method(net_namespace, "_normalizeArgs", (void*)js_net_normalizeArgs, 1);
    js_property_set(block_list_fn, make_string_item("isBlockList"),
                    js_new_function((void*)js_block_list_isBlockList, 1));

    Item default_key = make_string_item("default");
    js_property_set(net_namespace, default_key, net_namespace);

    net_socket_prototype = net_constructor_prototype(socket_fn, JS_CLASS_SOCKET);
    net_socket_connect_fn = js_new_function((void*)js_socket_connect, -1);
    js_property_set(net_socket_prototype, make_string_item("connect"), net_socket_connect_fn);
    js_property_set(stream_fn, make_string_item("prototype"), net_socket_prototype);
    if (get_type_id(stream_fn) == LMD_TYPE_FUNC) {
        js_function_set_prototype(stream_fn, net_socket_prototype);
    }

    net_server_prototype = net_constructor_prototype(server_fn, JS_CLASS_SERVER);
    js_property_set(create_server_fn, make_string_item("prototype"), net_server_prototype);
    if (get_type_id(create_server_fn) == LMD_TYPE_FUNC) {
        js_function_set_prototype(create_server_fn, net_server_prototype);
    }

    return net_namespace;
}

extern "C" void js_net_reset(void) {
    net_namespace = (Item){0};
    net_socket_prototype = (Item){0};
    net_server_prototype = (Item){0};
    net_socket_connect_fn = (Item){0};
    memset(net_active_sockets, 0, sizeof(net_active_sockets));
    memset(net_active_servers, 0, sizeof(net_active_servers));
    net_default_auto_select_family = false;
    net_auto_select_family_timeout = 250;
    memset(net_block_list_instances, 0, sizeof(net_block_list_instances));
    net_block_list_instance_count = 0;
}
