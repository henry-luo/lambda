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
#include "js_permission.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"
#include "../../lib/mem.h"

#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#endif

extern __thread EvalContext* context;
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" void js_clearTimeout(Item timer_id);
extern "C" Item js_buffer_from_bytes(const char* data, int len);
extern "C" Item js_new_aggregate_error(Item errors, Item message);
extern "C" void heap_register_gc_root_range(uint64_t* base, int count);
extern "C" Item js_throw_invalid_arg_type(const char* name, const char* expected, Item actual);
extern "C" Item js_throw_type_error_code(const char* code, const char* message);
extern "C" Item js_throw_out_of_range(const char* name, const char* range, Item actual);
extern "C" Item js_timeout_ref(Item this_val);
extern "C" Item js_timeout_unref(Item this_val);
extern "C" Item js_net_Socket(Item options);
extern "C" Item js_object_keys(Item object);
extern "C" void js_cluster_notify_worker_listening(Item address);
extern "C" bool js_process_ipc_worker_disconnect_requested(void);
extern "C" void js_process_ipc_notify_handle_accepted(void);
extern "C" void js_process_ipc_notify_socket_closed(void);
extern "C" uv_stream_t* js_http_stream_from_ipc_send_handle(Item handle_item);
extern "C" void* js_http_ipc_sent_stream_connection_account(uv_stream_t* stream);
extern "C" void js_http_complete_transferred_connection_account(void* account);
extern "C" bool js_http_close_ipc_sent_stream_defer_account(uv_stream_t* stream);
extern "C" bool js_http_close_ipc_sent_stream(uv_stream_t* stream);
extern "C" Item js_process_emit(Item event_name, Item arg1);
extern "C" Item js_process_emit2(Item event_name, Item arg1, Item arg2);
extern "C" Item js_net_get_socket_prototype(void);
extern "C" bool js_cluster_is_worker_runtime(void);
extern "C" bool js_cluster_worker_uses_sched_rr(void);

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

static void* net_native_ptr_from_item(Item item) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT) return (void*)(uintptr_t)it2i(item);
    if (type == LMD_TYPE_INT64) return (void*)(uintptr_t)it2l(item);
    return NULL;
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

// Net sockets default to a 64 KiB water mark; a 16 KiB generic stream default
// makes throttle tests fit in one kernel read before pause() can gate input.
#define NET_SOCKET_DEFAULT_HIGH_WATER_MARK (64 * 1024)

typedef struct PendingSocketWrite PendingSocketWrite;
typedef struct SocketAutoAttemptTimerReq SocketAutoAttemptTimerReq;
typedef struct JsServer JsServer;
typedef struct JsBoundSocket JsBoundSocket;

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
    bool      need_drain;
    bool      tos_set;
    bool      abort_handler_set;
    bool      abort_scheduled;
    bool      free_after_connect_pending;
    bool      onread_enabled;
    bool      onread_buffer_factory;
    bool      onread_external_alloc;
    bool      auto_select_family;
    bool      handle_closed_by_user;
    bool      adopted_bound_socket;
    bool      adopted_by_tls;
    bool      tls_close_notified;
    bool      ipc_received_socket;
    bool      transfer_account_pending;
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
    int       auto_attempt_timeout_ms;
    int       auto_attempt_generation;
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
    Item      tls_socket;
    Item      handle_close_object;
    PendingSocketWrite* pending_writes_head;
    PendingSocketWrite* pending_writes_tail;
    SocketAutoAttemptTimerReq* auto_attempt_timer;
    JsServer* owner_server;
} JsSocket;

struct SocketAutoAttemptTimerReq {
    uv_timer_t timer;
    JsSocket*  sock;
    int        generation;
};

struct JsBoundSocket {
    uv_tcp_t tcp;
    Item     js_object;
    bool     closed;
    bool     adopted;
};

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

typedef struct JsPipeHandle {
    uv_pipe_t pipe;
    Item      js_object;
    bool      closed;
} JsPipeHandle;

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

#define NET_PENDING_IPC_STREAM_MAX 128
typedef struct NetPendingIpcStream {
    uv_stream_t* stream;
    JsSocket*    sock;
    bool         close_seen;
    bool         cleanup_seen;
} NetPendingIpcStream;

static NetPendingIpcStream net_pending_ipc_streams[NET_PENDING_IPC_STREAM_MAX];

static NetPendingIpcStream* net_pending_ipc_find_stream(uv_stream_t* stream) {
    if (!stream) return NULL;
    for (int i = 0; i < NET_PENDING_IPC_STREAM_MAX; i++) {
        if (net_pending_ipc_streams[i].stream == stream) return &net_pending_ipc_streams[i];
    }
    return NULL;
}

static NetPendingIpcStream* net_pending_ipc_find_socket(JsSocket* sock) {
    if (!sock) return NULL;
    for (int i = 0; i < NET_PENDING_IPC_STREAM_MAX; i++) {
        if (net_pending_ipc_streams[i].sock == sock) return &net_pending_ipc_streams[i];
    }
    return NULL;
}

static void net_pending_ipc_register_socket(JsSocket* sock) {
    if (!sock) return;
    uv_stream_t* stream = (uv_stream_t*)&sock->tcp;
    if (net_pending_ipc_find_stream(stream)) return;
    for (int i = 0; i < NET_PENDING_IPC_STREAM_MAX; i++) {
        if (!net_pending_ipc_streams[i].stream) {
            net_pending_ipc_streams[i].stream = stream;
            net_pending_ipc_streams[i].sock = sock;
            return;
        }
    }
}

static void net_pending_ipc_clear(NetPendingIpcStream* entry) {
    if (!entry) return;
    memset(entry, 0, sizeof(NetPendingIpcStream));
}

static JsSocket* socket_from_object(Item self) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__handle__"));
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL || is_undefined_item(handle_item)) return NULL;
    // Native pointers can be boxed as INT64 in worker-mode heaps; accepting
    // only small INT makes valid sockets look detached.
    return (JsSocket*)net_native_ptr_from_item(handle_item);
}

static void socket_sync_no_half_open_listener(Item obj);

static bool net_is_object_like(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT || type == LMD_TYPE_VMAP ||
           type == LMD_TYPE_ELEMENT;
}

static bool net_object_has_key(Item obj, const char* key) {
    if (!net_is_object_like(obj) || !key) return false;
    Item keys = js_object_keys(obj);
    if (get_type_id(keys) != LMD_TYPE_ARRAY) return false;
    int64_t len = js_array_length(keys);
    size_t key_len = strlen(key);
    for (int64_t i = 0; i < len; i++) {
        Item item = js_array_get_int(keys, i);
        if (get_type_id(item) != LMD_TYPE_STRING) continue;
        String* s = it2s(item);
        if (s && (size_t)s->len == key_len && memcmp(s->chars, key, key_len) == 0) return true;
    }
    return false;
}

static Item socket_make_listener_record(Item listener, bool once) {
    Item record = js_new_object();
    js_property_set(record, make_string_item("listener"), listener);
    js_property_set(record, make_string_item("once"), (Item){.item = b2it(once)});
    return record;
}

static Item socket_listener_fn(Item record) {
    if (net_is_object_like(record)) {
        return js_property_get(record, make_string_item("listener"));
    }
    return record;
}

static bool socket_listener_once(Item record) {
    if (!net_is_object_like(record)) return false;
    Item once = js_property_get(record, make_string_item("once"));
    return get_type_id(once) == LMD_TYPE_BOOL && it2b(once);
}

static Item socket_listener_map(Item self, bool create) {
    Item listeners = js_property_get(self, make_string_item("__socket_listeners__"));
    if (!net_is_object_like(listeners)) {
        if (!create) return make_undefined_item();
        listeners = js_new_object();
        js_property_set(self, make_string_item("__socket_listeners__"), listeners);
    }
    return listeners;
}

static void socket_add_listener_item(Item self, Item event_item, Item callback, bool once) {
    if (get_type_id(event_item) != LMD_TYPE_STRING || !is_callable(callback)) return;
    Item listeners = socket_listener_map(self, true);
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        arr = js_array_new(0);
        js_property_set(listeners, event_item, arr);
    }
    js_array_push(arr, socket_make_listener_record(callback, once));
}

static void socket_add_listener_cstr(Item self, const char* event, Item callback, bool once) {
    if (!event) return;
    socket_add_listener_item(self, make_string_item(event), callback, once);
}

static int64_t socket_listener_count_item(Item self, Item event_item) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return 0;
    Item listeners = socket_listener_map(self, false);
    if (!net_is_object_like(listeners)) return 0;
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return 0;
    return js_array_length(arr);
}

static bool socket_has_listener(Item self, const char* event) {
    return socket_listener_count_item(self, make_string_item(event)) > 0;
}

static void socket_remove_listener_item(Item self, Item event_item, Item callback) {
    if (get_type_id(event_item) != LMD_TYPE_STRING || !is_callable(callback)) return;
    Item listeners = socket_listener_map(self, false);
    if (!net_is_object_like(listeners)) return;
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return;

    Item next = js_array_new(0);
    int64_t len = js_array_length(arr);
    bool removed = false;
    for (int64_t i = 0; i < len; i++) {
        Item record = js_array_get_int(arr, i);
        Item listener = socket_listener_fn(record);
        if (!removed && listener.item == callback.item) {
            removed = true;
            continue;
        }
        js_array_push(next, record);
    }
    js_property_set(listeners, event_item, next);
}

static void socket_remove_all_listeners(Item self, Item event_item) {
    Item listeners = socket_listener_map(self, false);
    if (!net_is_object_like(listeners)) return;
    if (is_undefined_item(event_item)) {
        js_property_set(self, make_string_item("__socket_listeners__"), js_new_object());
        socket_sync_no_half_open_listener(self);
        return;
    }
    if (get_type_id(event_item) != LMD_TYPE_STRING) return;
    js_property_set(listeners, event_item, js_array_new(0));
    String* ev = it2s(event_item);
    if (ev && ev->len == 3 && memcmp(ev->chars, "end", 3) == 0) {
        js_property_set(self, make_string_item("__no_half_open_listener__"), make_undefined_item());
        socket_sync_no_half_open_listener(self);
    }
}

static void socket_set_listener(Item obj, const char* event, Item callback) {
    socket_add_listener_cstr(obj, event, callback, false);
}

static void socket_close_now(JsSocket* sock);
static void socket_close_reset_now(JsSocket* sock);
static void socket_note_closed(JsSocket* sock);
static void socket_remove_abort_listener(JsSocket* sock);
static void socket_release_after_pending_connect(JsSocket* sock);
static void socket_configure_onread(JsSocket* sock, Item onread);
static bool socket_start_read(JsSocket* sock);
static bool socket_schedule_auto_retry(JsSocket* sock);
static void socket_shutdown_writes(JsSocket* sock, Item callback);
static void socket_update_writable(JsSocket* sock, bool writable);
static void socket_update_readable(JsSocket* sock, bool readable);
static Item make_uv_error(int status, const char* syscall, const char* host, int port);
static Item make_socket_handle_object(JsSocket* sock);
static bool socket_delegate_close_to_tls(JsSocket* sock, Item error_item);

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

static void socket_free_detached(JsSocket* sock) {
    if (!sock) return;
    NetPendingIpcStream* pending = net_pending_ipc_find_socket(sock);
    if (pending && !pending->cleanup_seen) {
        // descriptor-transfer cleanup can trail the socket close callback; keep
        // wrapper storage until IPC cleanup can identify the net stream safely.
        pending->close_seen = true;
        sock->tcp.data = NULL;
        return;
    }
    if (pending) net_pending_ipc_clear(pending);
    // IPC descriptor cleanup may retain the uv_stream_t pointer after close;
    // detach handle data before release so stale callbacks cannot see a wrapper.
    sock->tcp.data = NULL;
    mem_free(sock);
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

static void socket_auto_attempt_timer_close_cb(uv_handle_t* handle) {
    SocketAutoAttemptTimerReq* req = handle ? (SocketAutoAttemptTimerReq*)handle->data : NULL;
    if (req) mem_free(req);
}

static void socket_clear_auto_attempt_timer(JsSocket* sock) {
    if (!sock || !sock->auto_attempt_timer) return;
    SocketAutoAttemptTimerReq* req = sock->auto_attempt_timer;
    sock->auto_attempt_timer = NULL;
    req->sock = NULL;
    if (!uv_is_closing((uv_handle_t*)&req->timer)) {
        uv_timer_stop(&req->timer);
        uv_close((uv_handle_t*)&req->timer, socket_auto_attempt_timer_close_cb);
    }
}

static void socket_auto_attempt_timer_cb(uv_timer_t* timer) {
    SocketAutoAttemptTimerReq* req = timer ? (SocketAutoAttemptTimerReq*)timer->data : NULL;
    JsSocket* sock = req ? req->sock : NULL;
    if (sock && sock->auto_attempt_timer == req) sock->auto_attempt_timer = NULL;
    if (req && !uv_is_closing((uv_handle_t*)&req->timer)) {
        uv_timer_stop(&req->timer);
        uv_close((uv_handle_t*)&req->timer, socket_auto_attempt_timer_close_cb);
    }
    if (!sock || sock->destroyed || req->generation != sock->auto_attempt_generation) return;
    if (!sock->connect_pending || sock->connected) return;
    if (sock->auto_addr_index >= sock->auto_addr_count) return;
    // auto-select attempts can black-hole in the kernel; retry on Node's
    // bounded attempt window instead of waiting for OS connect timeout.
    socket_schedule_auto_retry(sock);
}

static void socket_start_auto_attempt_timer(JsSocket* sock) {
    if (!sock || sock->auto_attempt_timeout_ms <= 0) return;
    socket_clear_auto_attempt_timer(sock);
    if (sock->auto_addr_index >= sock->auto_addr_count) return;
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return;
    SocketAutoAttemptTimerReq* req =
        (SocketAutoAttemptTimerReq*)mem_calloc(1, sizeof(SocketAutoAttemptTimerReq), MEM_CAT_JS_RUNTIME);
    if (!req) return;
    if (uv_timer_init(loop, &req->timer) != 0) {
        mem_free(req);
        return;
    }
    sock->auto_attempt_generation++;
    req->sock = sock;
    req->generation = sock->auto_attempt_generation;
    req->timer.data = req;
    sock->auto_attempt_timer = req;
    int r = uv_timer_start(&req->timer, socket_auto_attempt_timer_cb,
                           (uint64_t)sock->auto_attempt_timeout_ms, 0);
    if (r != 0) {
        sock->auto_attempt_timer = NULL;
        uv_close((uv_handle_t*)&req->timer, socket_auto_attempt_timer_close_cb);
        return;
    }
    uv_unref((uv_handle_t*)&req->timer);
}

static void socket_update_io_counters(JsSocket* sock) {
    if (!sock) return;
    js_property_set(sock->js_object, make_string_item("bytesRead"),
                    (Item){.item = i2it(sock->bytes_read)});
    js_property_set(sock->js_object, make_string_item("bytesWritten"),
                    (Item){.item = i2it(sock->bytes_written)});
    js_property_set(sock->js_object, make_string_item("bufferSize"),
                    (Item){.item = i2it(sock->buffer_size)});
    js_property_set(sock->js_object, make_string_item("writableLength"),
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
    Item listeners = socket_listener_map(obj, false);
    if (!net_is_object_like(listeners)) return;

    Item event_item = make_string_item(event);
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return;

    int64_t len = js_array_length(arr);
    if (len <= 0) return;
    Item next = js_array_new(0);
    for (int64_t i = 0; i < len; i++) {
        Item record = js_array_get_int(arr, i);
        Item callback = socket_listener_fn(record);
        bool once = socket_listener_once(record);
        if (is_callable(callback)) {
            js_call_function(callback, obj, args, argc);
            js_microtask_flush();
        }
        if (!once) js_array_push(next, record);
    }
    js_property_set(listeners, event_item, next);
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
    socket_update_writable(sock, false);
    if (sock->finished) {
        socket_close_now(sock);
        return;
    }
    // Non-half-open sockets must send their own FIN after peer EOF; emitting
    // finish alone leaves a live TCP handle until the test harness drain timeout.
    socket_shutdown_writes(sock, make_undefined_item());
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
    Item pipe_dest = js_property_get(sock->js_object, make_string_item("__pipe_dest__"));
    bool has_pipe = pipe_dest.item != 0 && pipe_dest.item != ITEM_NULL && !is_undefined_item(pipe_dest);
    if (!socket_has_listener(sock->js_object, "data") && !has_pipe) return;
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

extern "C" Item js_socket_on(Item event_item, Item callback) {
    Item self = js_get_this();
    socket_add_listener_item(self, event_item, callback, false);
    if (get_type_id(event_item) == LMD_TYPE_STRING) {
        String* ev = it2s(event_item);
        if (ev && ev->len == 4 && memcmp(ev->chars, "data", 4) == 0) {
            JsSocket* sock = socket_from_object(self);
            if (sock && !sock->destroyed) {
                // ipc-adopted sockets are intentionally delivered paused; the
                // data listener is the userland readiness edge that starts reads.
                sock->paused = false;
                socket_start_read(sock);
            }
        }
    }
    return self;
}

extern "C" Item js_socket_once(Item event_item, Item callback) {
    Item self = js_get_this();
    socket_add_listener_item(self, event_item, callback, true);
    if (get_type_id(event_item) == LMD_TYPE_STRING) {
        String* ev = it2s(event_item);
        if (ev && ev->len == 4 && memcmp(ev->chars, "data", 4) == 0) {
            JsSocket* sock = socket_from_object(self);
            if (sock && !sock->destroyed) {
                // ipc-adopted sockets are intentionally delivered paused; the
                // data listener is the userland readiness edge that starts reads.
                sock->paused = false;
                socket_start_read(sock);
            }
        }
    }
    return self;
}

extern "C" Item js_socket_listeners(Item event_item) {
    Item self = js_get_this();
    Item result = js_array_new(0);
    if (get_type_id(event_item) != LMD_TYPE_STRING) return result;
    Item listeners = socket_listener_map(self, false);
    if (!net_is_object_like(listeners)) return result;
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return result;
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item callback = socket_listener_fn(js_array_get_int(arr, i));
        if (is_callable(callback)) js_array_push(result, callback);
    }
    return result;
}

extern "C" Item js_socket_listenerCount(Item event_item) {
    Item self = js_get_this();
    return (Item){.item = i2it(socket_listener_count_item(self, event_item))};
}

extern "C" Item js_socket_removeListener(Item event_item, Item callback) {
    Item self = js_get_this();
    socket_remove_listener_item(self, event_item, callback);
    return self;
}

extern "C" Item js_socket_removeAllListeners(Item event_item) {
    Item self = js_get_this();
    socket_remove_all_listeners(self, event_item);
    return self;
}

static Item js_socket_no_half_open_listener(void) {
    return make_undefined_item();
}

static void socket_sync_no_half_open_listener(Item obj) {
    Item allow = js_property_get(obj, make_string_item("allowHalfOpen"));
    bool allow_half_open = get_type_id(allow) == LMD_TYPE_BOOL && it2b(allow);
    Item existing = js_property_get(obj, make_string_item("__no_half_open_listener__"));
    if (allow_half_open) {
        if (is_callable(existing)) {
            socket_remove_listener_item(obj, make_string_item("end"), existing);
            js_property_set(obj, make_string_item("__no_half_open_listener__"), make_undefined_item());
        }
        return;
    }
    if (is_callable(existing)) return;
    Item listener = js_new_function((void*)js_socket_no_half_open_listener, 0);
    js_property_set(obj, make_string_item("__no_half_open_listener__"), listener);
    socket_add_listener_cstr(obj, "end", listener, false);
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

static Item socket_emit_error_close_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    Item err = env[1];
    socket_emit(self, "error", &err, 1);
    JsSocket* sock = socket_from_object(self);
    if (sock && !sock->destroyed) socket_close_now(sock);
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

static void socket_schedule_error_close_event(JsSocket* sock, Item err) {
    if (!sock || !sock->js_object.item) return;
    Item* env = js_alloc_env(2);
    env[0] = sock->js_object;
    env[1] = err;
    Item fn = js_new_closure((void*)socket_emit_error_close_scheduled, 0, env, 2);
    js_next_tick_enqueue(fn);
}

static bool socket_delegate_close_to_tls(JsSocket* sock, Item error_item) {
    if (!sock || !sock->adopted_by_tls || sock->tls_close_notified) return false;
    sock->destroyed = true;
    sock->reading = false;
    socket_clear_auto_attempt_timer(sock);
    socket_clear_timeout(sock);
    socket_remove_abort_listener(sock);
    socket_update_writable(sock, false);
    socket_update_readable(sock, false);
    js_property_set(sock->js_object, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
    socket_update_state_properties(sock);
    if (!is_undefined_item(error_item) && error_item.item != ITEM_NULL) {
        socket_emit(sock->js_object, "error", &error_item, 1);
    }
    Item destroy_fn = js_property_get(sock->tls_socket, make_string_item("destroy"));
    if (is_callable(destroy_fn)) {
        // TLS owns the shared uv_tcp_t after adoption; net may only request
        // TLSSocket teardown or both objects can close the same libuv handle.
        js_call_function(destroy_fn, sock->tls_socket, NULL, 0);
        js_microtask_flush();
    }
    return true;
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

static void socket_maybe_emit_drain(JsSocket* sock) {
    if (!sock || sock->destroyed || !sock->need_drain) return;
    size_t write_queue_size = uv_stream_get_write_queue_size((uv_stream_t*)&sock->tcp);
    if (sock->buffer_size > sock->high_water_mark ||
        (int64_t)write_queue_size > sock->high_water_mark) {
        return;
    }
    // write(false) establishes a drain edge; without replaying it after libuv
    // flushes, callers stop producing data and the socket waits for the watchdog.
    sock->need_drain = false;
    socket_emit(sock->js_object, "drain", NULL, 0);
}

static void socket_handle_remote_eof(JsSocket* sock) {
    if (!sock || sock->destroyed) return;
    sock->remote_ended = true;
    js_property_set(sock->js_object, make_string_item("__remote_ended__"), (Item){.item = ITEM_TRUE});
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
            socket_maybe_emit_drain(wreq->sock);
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

    size_t write_queue_size = uv_stream_get_write_queue_size((uv_stream_t*)&sock->tcp);
    // Lambda tracks in-flight write bytes separately from libuv; using only
    // uv's queue lets large writes report true and spin official drain loops.
    bool below_hwm = sock->buffer_size <= sock->high_water_mark &&
                     (int64_t)write_queue_size <= sock->high_water_mark;
    if (!below_hwm) sock->need_drain = true;
    return (Item){.item = b2it(below_hwm)};
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
    if (sock->buffer_size > sock->high_water_mark) sock->need_drain = true;
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
    socket_free_detached(sock);
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
    // The borrowed net.Socket remains the raw transport after TLS adoption;
    // routing its writes through TLS hides fixtures that inject malformed records.
    Item remote_ended_marker = js_property_get(self, make_string_item("__remote_ended__"));
    if (((sock && sock->remote_ended) || remote_ended_marker.item == ITEM_TRUE) &&
        !socket_allow_half_open(self)) {
        // A peer FIN can clear/free the native handle before the next write;
        // preserve that semantic state as EPIPE instead of generic stream destroy.
        Item err = socket_make_error("EPIPE", "This socket has been ended by the other party");
        socket_report_write_error(sock, callback, err);
        socket_schedule_error_event(sock, err);
        return (Item){.item = b2it(false)};
    }
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
    // Borrowed raw socket end() may intentionally write non-TLS bytes; only
    // close/destroy ownership is delegated to TLSSocket after adoption.
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
    if (socket_delegate_close_to_tls(sock, error_item)) return self;

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
    if (socket_delegate_close_to_tls(sock, error_item)) return self;

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
    if (socket_delegate_close_to_tls(sock, make_undefined_item())) return;
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        sock->reading = false;
        sock->destroyed = true;
        socket_clear_auto_attempt_timer(sock);
        socket_clear_timeout(sock);
        socket_remove_abort_listener(sock);
        socket_hide_handle(sock);
        socket_update_writable(sock, false);
        js_property_set(sock->js_object, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
        socket_update_state_properties(sock);
        uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
            JsSocket* s = (JsSocket*)handle->data;
            if (s) {
                bool notify_ipc_parent = s->ipc_received_socket;
                net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, s->js_object);
                js_property_set(s->js_object, make_string_item("__handle__"), ItemNull);
                if (notify_ipc_parent) js_process_ipc_notify_socket_closed();
                socket_emit_close(s->js_object, false);
                socket_note_closed(s);
                if (s->connect_pending) {
                    s->free_after_connect_pending = true;
                } else {
                    socket_free_detached(s);
                }
            }
        });
    }
}

static void socket_close_reset_now(JsSocket* sock) {
    if (!sock) return;
    if (socket_delegate_close_to_tls(sock, make_undefined_item())) return;
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        sock->reading = false;
        sock->destroyed = true;
        socket_clear_auto_attempt_timer(sock);
        socket_clear_timeout(sock);
        socket_remove_abort_listener(sock);
        socket_hide_handle(sock);
        socket_update_writable(sock, false);
        js_property_set(sock->js_object, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
        socket_update_state_properties(sock);
        int r = uv_tcp_close_reset(&sock->tcp, [](uv_handle_t* handle) {
            JsSocket* s = (JsSocket*)handle->data;
            if (s) {
                bool notify_ipc_parent = s->ipc_received_socket;
                net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, s->js_object);
                js_property_set(s->js_object, make_string_item("__handle__"), ItemNull);
                if (notify_ipc_parent) js_process_ipc_notify_socket_closed();
                socket_emit_close(s->js_object, false);
                socket_note_closed(s);
                if (s->connect_pending) {
                    s->free_after_connect_pending = true;
                } else {
                    socket_free_detached(s);
                }
            }
        });
        if (r != 0 && !uv_is_closing((uv_handle_t*)&sock->tcp)) {
            uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* handle) {
                JsSocket* s = (JsSocket*)handle->data;
                if (s) {
                    bool notify_ipc_parent = s->ipc_received_socket;
                    net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, s->js_object);
                    js_property_set(s->js_object, make_string_item("__handle__"), ItemNull);
                    if (notify_ipc_parent) js_process_ipc_notify_socket_closed();
                    socket_emit_close(s->js_object, false);
                    socket_note_closed(s);
                    if (s->connect_pending) {
                        s->free_after_connect_pending = true;
                    } else {
                        socket_free_detached(s);
                    }
                }
            });
        }
    }
}

extern "C" uv_tcp_t* js_net_socket_adopt_for_tls(Item socket_obj, Item tls_obj) {
    JsSocket* sock = socket_from_object(socket_obj);
    if (!sock || sock->destroyed || sock->adopted_by_tls) return NULL;
    if (uv_is_closing((uv_handle_t*)&sock->tcp)) return NULL;
    if (sock->reading) {
        uv_read_stop((uv_stream_t*)&sock->tcp);
        sock->reading = false;
    }
    sock->paused = true;
    sock->adopted_by_tls = true;
    sock->tls_close_notified = false;
    sock->tls_socket = tls_obj;
    js_property_set(sock->js_object, make_string_item("__tls_socket__"), tls_obj);
    // From this point TLS is the sole native owner; net keeps JS state only
    // so borrowed-socket close paths cannot interpret handle->data as JsSocket.
    return &sock->tcp;
}

extern "C" void js_net_socket_tls_closed(Item socket_obj, bool had_error) {
    JsSocket* sock = socket_from_object(socket_obj);
    if (!sock || sock->tls_close_notified) return;
    sock->tls_close_notified = true;
    sock->adopted_by_tls = false;
    sock->destroyed = true;
    sock->connected = false;
    sock->reading = false;
    socket_clear_auto_attempt_timer(sock);
    socket_clear_timeout(sock);
    socket_remove_abort_listener(sock);
    socket_hide_handle(sock);
    socket_update_writable(sock, false);
    socket_update_readable(sock, false);
    js_property_set(sock->js_object, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
    js_property_set(sock->js_object, make_string_item("__handle__"), ItemNull);
    js_property_set(sock->js_object, make_string_item("__tls_socket__"), make_undefined_item());
    socket_update_state_properties(sock);
    net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, sock->js_object);
    socket_emit_close(sock->js_object, had_error);
    socket_note_closed(sock);
    socket_free_detached(sock);
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

static bool socket_keep_alive_seconds(Item value, int* out_secs) {
    if (!out_secs) return false;
    if (is_undefined_item(value) || value.item == ITEM_NULL) return false;
    Item num = js_to_number(value);
    double d = net_number_value(num);
    *out_secs = d > 0 ? (int)(d / 1000.0) : 0;
    return true;
}

static Item js_socket_setKeepAlive(Item rest_args) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_object(self);
    int64_t argc64 = js_array_length(rest_args);
    int argc = argc64 > 16 ? 16 : (int)argc64;
    Item first = argc > 0 ? js_array_get_int(rest_args, 0) : make_undefined_item();
    Item delay = argc > 1 ? js_array_get_int(rest_args, 1) : make_undefined_item();
    Item interval = argc > 2 ? js_array_get_int(rest_args, 2) : make_undefined_item();
    Item count = argc > 3 ? js_array_get_int(rest_args, 3) : make_undefined_item();

    bool options_object = net_is_object_like(first);
    bool enable_bool = false;
    if (options_object) {
        Item opt_enable = js_property_get(first, make_string_item("enable"));
        enable_bool = js_is_truthy(opt_enable);
        delay = js_property_get(first, make_string_item("initialDelay"));
        interval = js_property_get(first, make_string_item("interval"));
        count = js_property_get(first, make_string_item("count"));
    } else {
        enable_bool = js_is_truthy(first);
    }

    bool has_delay = !is_undefined_item(delay) && delay.item != ITEM_NULL;
    bool has_interval = !is_undefined_item(interval) && interval.item != ITEM_NULL;
    int delay_secs = 0;
    int interval_secs = 0;
    if (has_delay) socket_keep_alive_seconds(delay, &delay_secs);
    if (has_interval) socket_keep_alive_seconds(interval, &interval_secs);
    if (sock) {
        if (sock->keep_alive_requested == enable_bool &&
            (!has_delay || sock->keep_alive_delay_secs == delay_secs) &&
            !has_interval && (is_undefined_item(count) || count.item == ITEM_NULL)) {
            return self;
        }
        sock->keep_alive_requested = enable_bool;
        if (has_delay) sock->keep_alive_delay_secs = delay_secs;
    }

    Item handle = js_property_get(self, make_string_item("_handle"));
    if (handle.item != 0 && handle.item != ITEM_NULL && !is_undefined_item(handle)) {
        Item fn = js_property_get(handle, make_string_item("setKeepAlive"));
        if (is_callable(fn)) {
            Item raw_delay = has_delay ? (Item){.item = i2it(delay_secs)} : delay;
            Item raw_interval = has_interval ? (Item){.item = i2it(interval_secs)} : interval;
            Item args[4] = {
                (Item){.item = b2it(enable_bool)},
                raw_delay,
                raw_interval,
                count
            };
            js_call_function(fn, handle, args, 4);
            js_microtask_flush();
        }
    }
    return self;
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
    // IPC/internal handles carry native pointers through JS properties; worker
    // heaps can box them as INT64 even when primary heaps use INT.
    return (JsSocket*)net_native_ptr_from_item(handle_item);
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

static void socket_handle_close_cb(uv_handle_t* handle) {
    JsSocket* sock = handle ? (JsSocket*)handle->data : NULL;
    if (!sock) return;
    Item handle_obj = sock->handle_close_object;
    if (handle_obj.item) {
        Item callback = js_property_get(handle_obj, make_string_item("__handle_close_callback__"));
        js_property_set(handle_obj, make_string_item("__handle_close_callback__"), make_undefined_item());
        if (is_callable(callback)) {
            js_call_function(callback, handle_obj, NULL, 0);
            js_microtask_flush();
        }
        net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, handle_obj);
        sock->handle_close_object = make_undefined_item();
    }
    socket_note_closed(sock);
    if (!sock->js_object.item || sock->js_object.item == handle_obj.item) {
        socket_free_detached(sock);
    }
}

static Item js_socket_handle_close(Item callback) {
    Item self = js_get_this();
    JsSocket* sock = socket_from_handle_object(self);
    if (!sock) return make_undefined_item();
    sock->handle_closed_by_user = true;
    if (!uv_is_closing((uv_handle_t*)&sock->tcp)) {
        if (is_callable(callback)) {
            // raw accepted handles can be closed before they are wrapped as
            // sockets; root the handle until libuv runs the close callback.
            js_property_set(self, make_string_item("__handle_close_callback__"), callback);
            sock->handle_close_object = self;
            net_active_add(net_active_sockets, NET_ACTIVE_SOCKET_MAX, self);
        }
        uv_close((uv_handle_t*)&sock->tcp, socket_handle_close_cb);
    } else if (is_callable(callback)) {
        js_call_function(callback, self, NULL, 0);
        js_microtask_flush();
    }
    return make_undefined_item();
}

static JsPipeHandle* pipe_handle_from_object(Item self) {
    if (!net_is_object_like(self)) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__pipe_handle__"));
    return (JsPipeHandle*)net_native_ptr_from_item(handle_item);
}

static void pipe_handle_close_cb(uv_handle_t* handle) {
    JsPipeHandle* ph = handle ? (JsPipeHandle*)handle->data : NULL;
    if (!ph) return;
    net_active_remove(net_active_sockets, NET_ACTIVE_SOCKET_MAX, ph->js_object);
    mem_free(ph);
}

static Item js_pipe_handle_close(Item callback) {
    (void)callback;
    Item self = js_get_this();
    JsPipeHandle* ph = pipe_handle_from_object(self);
    if (!ph || ph->closed) return make_undefined_item();
    ph->closed = true;
    if (!uv_is_closing((uv_handle_t*)&ph->pipe)) {
        uv_close((uv_handle_t*)&ph->pipe, pipe_handle_close_cb);
    }
    return make_undefined_item();
}

static Item make_pipe_handle_object(JsPipeHandle* ph) {
    Item obj = js_new_object();
    if (!ph) return obj;
    ph->js_object = obj;
    js_property_set(obj, make_string_item("__pipe_handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ph)});
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_pipe_handle_close, 1));
    net_active_add(net_active_sockets, NET_ACTIVE_SOCKET_MAX, obj);
    return obj;
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

static bool socket_has_js_read_handle(JsSocket* sock, Item* out_handle) {
    if (!sock || !sock->js_object.item) return false;
    Item handle = js_property_get(sock->js_object, make_string_item("_handle"));
    if (!net_is_object_like(handle)) return false;
    Item native_handle = js_property_get(handle, make_string_item("__socket_handle__"));
    if (get_type_id(native_handle) == LMD_TYPE_INT) return false;
    Item read_start = js_property_get(handle, make_string_item("readStart"));
    if (!is_callable(read_start)) return false;
    if (out_handle) *out_handle = handle;
    return true;
}

static Item js_socket_js_handle_close_done(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    JsSocket* sock = socket_from_object(self);
    if (sock && !sock->destroyed) {
        sock->destroyed = true;
        socket_update_readable(sock, false);
        socket_update_writable(sock, false);
        socket_update_state_properties(sock);
    }
    socket_emit(self, "close", NULL, 0);
    return make_undefined_item();
}

static Item js_socket_js_handle_onread(void) {
    Item handle = js_get_this();
    Item self = js_property_get(handle, make_string_item("__socket_object__"));
    JsSocket* sock = socket_from_object(self);
    if (!sock || sock->remote_ended) return make_undefined_item();

    sock->remote_ended = true;
    sock->reading = false;
    socket_update_readable(sock, false);
    socket_emit(self, "end", NULL, 0);

    Item* env = js_alloc_env(1);
    env[0] = self;
    Item close_done = js_new_closure((void*)js_socket_js_handle_close_done, 0, env, 1);
    Item close_fn = js_property_get(handle, make_string_item("close"));
    if (is_callable(close_fn)) {
        js_call_function(close_fn, handle, &close_done, 1);
        js_microtask_flush();
    } else {
        js_socket_js_handle_close_done((Item){.item = i2it((int64_t)(uintptr_t)env)});
    }
    return make_undefined_item();
}

static void client_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
static void server_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
static void server_client_read_cb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

static bool socket_start_read(JsSocket* sock) {
    if (!sock || sock->destroyed || sock->reading) return false;
    if (sock->adopted_by_tls) return false;
    Item js_handle = make_undefined_item();
    if (socket_has_js_read_handle(sock, &js_handle)) {
        Item read_start = js_property_get(js_handle, make_string_item("readStart"));
        sock->reading = true;
        js_call_function(read_start, js_handle, NULL, 0);
        js_microtask_flush();
        return true;
    }
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
        if (sock->adopted_by_tls) return self;
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
        if (sock->adopted_by_tls) return self;
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

extern "C" Item js_net_createServer(Item rest_args);

static Item net_socket_prototype = {0};
static Item net_server_prototype = {0};
static Item net_socket_connect_fn = {0};
static Item net_namespace = {0};
static bool net_default_auto_select_family = false;
static int net_auto_select_family_timeout = 500; // Node.js default
static bool net_cli_options_applied = false;

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
                    js_new_function((void*)js_socket_handle_close, 1));
    return handle;
}

// create a JS socket object wrapping a JsSocket
static Item make_socket_object(JsSocket* sock, bool expose_handle) {
    if (sock->high_water_mark < 0) sock->high_water_mark = NET_SOCKET_DEFAULT_HIGH_WATER_MARK;
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
    js_property_set(obj, make_string_item("listeners"),
                    js_new_function((void*)js_socket_listeners, 1));
    js_property_set(obj, make_string_item("listenerCount"),
                    js_new_function((void*)js_socket_listenerCount, 1));
    js_property_set(obj, make_string_item("removeListener"),
                    js_new_function((void*)js_socket_removeListener, 2));
    js_property_set(obj, make_string_item("off"),
                    js_new_function((void*)js_socket_removeListener, 2));
    js_property_set(obj, make_string_item("removeAllListeners"),
                    js_new_function((void*)js_socket_removeAllListeners, 1));
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
    js_property_set(obj, make_string_item("writableLength"), (Item){.item = i2it(0)});
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
                    js_new_function((void*)js_socket_setKeepAlive, -1));
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

static bool net_fd_is_listening_socket(int fd) {
#ifdef _WIN32
    (void)fd;
    return false;
#else
    int accepting = 0;
    socklen_t accepting_len = sizeof(accepting);
    if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &accepting_len) == 0 &&
        accepting != 0) {
        return true;
    }
    struct sockaddr_storage peer;
    socklen_t peer_len = sizeof(peer);
    if (getpeername(fd, (struct sockaddr*)&peer, &peer_len) == 0) return false;
    // Descriptor-passed listening TCP handles can report SO_ACCEPTCONN=0 on
    // some platforms; unlike transferred sockets, they still have no peer.
    return errno == ENOTCONN;
#endif
}

static Item make_server_object_from_fd(uv_loop_t* loop, int fd);
static Item js_server_address_for(Item self);
extern "C" Item js_get_net_namespace(void);
static Item make_pipe_handle_object(JsPipeHandle* ph);

extern "C" Item js_net_accept_ipc_tcp_handle(uv_pipe_t* pipe) {
    if (!pipe || uv_pipe_pending_count(pipe) <= 0) {
        return make_undefined_item();
    }

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return make_undefined_item();

    uv_handle_type pending_type = uv_pipe_pending_type(pipe);
    if (pending_type == UV_NAMED_PIPE) {
        JsPipeHandle* ph = (JsPipeHandle*)mem_calloc(1, sizeof(JsPipeHandle), MEM_CAT_JS_RUNTIME);
        if (!ph) return make_undefined_item();
        uv_pipe_init(loop, &ph->pipe, 0);
        ph->pipe.data = ph;
        int r = uv_accept((uv_stream_t*)pipe, (uv_stream_t*)&ph->pipe);
        if (r != 0) {
            uv_close((uv_handle_t*)&ph->pipe, [](uv_handle_t* h) {
                JsPipeHandle* p = h ? (JsPipeHandle*)h->data : NULL;
                if (p) mem_free(p);
            });
            return make_undefined_item();
        }
        js_process_ipc_notify_handle_accepted();
        // Cluster pipe listeners pass named-pipe endpoints, not TCP sockets;
        // accepting only UV_TCP leaves primary-owned pipe dispatch unusable.
        return make_pipe_handle_object(ph);
    }

    if (pending_type != UV_TCP) {
        return make_undefined_item();
    }

    uv_tcp_t* accepted = (uv_tcp_t*)mem_calloc(1, sizeof(uv_tcp_t), MEM_CAT_JS_RUNTIME);
    if (!accepted) return make_undefined_item();
    uv_tcp_init(loop, accepted);
    accepted->data = accepted;

    int r = uv_accept((uv_stream_t*)pipe, (uv_stream_t*)accepted);
    if (r != 0) {
        // pending IPC handles are one-shot; close the initialized wrapper when
        // adoption fails so the rejected descriptor does not linger in libuv.
        uv_close((uv_handle_t*)accepted, [](uv_handle_t* h) {
            if (h) mem_free(h);
        });
        return make_undefined_item();
    }
    js_process_ipc_notify_handle_accepted();

#ifdef _WIN32
    int fd = -1;
#else
    uv_os_fd_t raw_fd;
    int fd = -1;
    if (uv_fileno((const uv_handle_t*)accepted, &raw_fd) == 0) {
        fd = dup((int)raw_fd);
    }
#endif
    bool is_listener = fd >= 0 && net_fd_is_listening_socket(fd);
    uv_close((uv_handle_t*)accepted, [](uv_handle_t* h) {
        if (h) mem_free(h);
    });
    if (fd < 0) return make_undefined_item();

    if (is_listener) {
        if (get_type_id(net_server_prototype) != LMD_TYPE_MAP) {
            // IPC can deliver a server before userland touches net.Server in
            // this process; initialize prototypes so transferred servers brand correctly.
            js_get_net_namespace();
        }
        // IPC TCP handles can be listening servers; treating them as sockets
        // breaks cluster server transfer and leaves worker IPC alive to timeout.
        return make_server_object_from_fd(loop, fd);
    }

    if (get_type_id(net_socket_prototype) != LMD_TYPE_MAP) {
        // IPC can deliver a socket before userland touches net.Socket in this
        // process; initialize prototypes so transferred sockets pass instanceof.
        js_get_net_namespace();
    }

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    if (!sock) {
#ifndef _WIN32
        close(fd);
#endif
        return make_undefined_item();
    }
    sock->high_water_mark = NET_SOCKET_DEFAULT_HIGH_WATER_MARK;
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;
    if (uv_tcp_open(&sock->tcp, (uv_os_sock_t)fd) != 0) {
#ifndef _WIN32
        close(fd);
#endif
        uv_close((uv_handle_t*)&sock->tcp, [](uv_handle_t* h) {
            JsSocket* s = h ? (JsSocket*)h->data : NULL;
            socket_free_detached(s);
        });
        return make_undefined_item();
    }

    Item obj = make_socket_object(sock, true);
    sock->connected = true;
    sock->is_server_side = true;
    sock->ipc_received_socket = true;
    socket_update_state_properties(sock);
    socket_update_address_properties(sock);
    net_active_add(net_active_sockets, NET_ACTIVE_SOCKET_MAX, obj);
    // IPC-adopted sockets are delivered paused; eager uv_read_start can close
    // a delayed SCM_RIGHTS descriptor before userland performs the first write.
    return obj;
}

// =============================================================================
// net.createConnection(port, host) — TCP client
// =============================================================================

typedef struct NetConnectOptions {
    int port;
    int family;
    char host[256];
    bool has_path;
    char path[256];
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
    int auto_select_family_attempt_timeout;
    bool allow_half_open;
} NetConnectOptions;

static bool net_permission_allowed(void) {
    return js_permission_has_net() != 0;
}

static Item make_net_permission_error(const NetConnectOptions* options) {
    char resource[320];
    resource[0] = '\0';
    if (options) {
        if (options->has_path) {
            snprintf(resource, sizeof(resource), "%s", options->path);
        } else if (options->host[0]) {
            snprintf(resource, sizeof(resource), "%s:%d", options->host, options->port);
        }
    }
    return js_permission_make_net_error("connect", resource[0] ? resource : NULL);
}

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
    int auto_select_family_attempt_timeout;
    Item lookup;
    Item block_list;
    bool has_block_list;
} NetResolveReq;

typedef struct NetPipeConnectReq {
    uv_connect_t req;
    uv_pipe_t    pipe;
    JsSocket*    sock;
    char         path[4096];
} NetPipeConnectReq;

static void client_connect_cb(uv_connect_t* req, int status);
static void socket_close_now(JsSocket* sock);

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

static Item make_path_connect_error(int status, const char* path) {
    const char* code = uv_err_name(status);
    char msg[512];
    snprintf(msg, sizeof(msg), "connect %s %s", code, path ? path : "");
    Item err = js_new_error(make_string_item(msg));
    js_property_set(err, make_string_item("code"), make_string_item(code));
    js_property_set(err, make_string_item("errno"), (Item){.item = i2it(status)});
    js_property_set(err, make_string_item("syscall"), make_string_item("connect"));
    if (path) js_property_set(err, make_string_item("path"), make_string_item(path));
    return err;
}

static Item make_invalid_ip_address_error(Item address) {
    Item address_str = js_to_string(address);
    char value[128] = "";
    if (get_type_id(address_str) == LMD_TYPE_STRING) {
        String* s = it2s(address_str);
        int len = (int)s->len < (int)sizeof(value) - 1 ? (int)s->len : (int)sizeof(value) - 1;
        memcpy(value, s->chars, (size_t)len);
        value[len] = '\0';
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "Invalid IP address: %s", value);
    Item err = js_new_error(make_string_item(msg));
    js_property_set(err, make_string_item("code"), make_string_item("ERR_INVALID_IP_ADDRESS"));
    return err;
}

static Item throw_invalid_ip_address(Item address) {
    Item address_str = js_to_string(address);
    char value[128] = "";
    if (get_type_id(address_str) == LMD_TYPE_STRING) {
        String* s = it2s(address_str);
        int len = (int)s->len < (int)sizeof(value) - 1 ? (int)s->len : (int)sizeof(value) - 1;
        memcpy(value, s->chars, (size_t)len);
        value[len] = '\0';
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "Invalid IP address: %s", value);
    return js_throw_type_error_code("ERR_INVALID_IP_ADDRESS", msg);
}

static Item make_invalid_address_family_error(int family, const char* host, int port) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Invalid address family: %d %s:%d",
             family, host ? host : "", port);
    Item err = js_new_error(make_string_item(msg));
    js_property_set(err, make_string_item("code"), make_string_item("ERR_INVALID_ADDRESS_FAMILY"));
    if (host) js_property_set(err, make_string_item("host"), make_string_item(host));
    js_property_set(err, make_string_item("port"), (Item){.item = i2it(port)});
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

static bool parse_local_port(Item value, int* out_port) {
    TypeId type = get_type_id(value);
    if (type != LMD_TYPE_INT && type != LMD_TYPE_INT64 && type != LMD_TYPE_FLOAT) {
        js_throw_invalid_arg_type("options.localPort", "number", value);
        return false;
    }
    return parse_port(value, out_port);
}

static bool copy_string_item(Item value, char* out, int out_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return false;
    String* s = it2s(value);
    int len = (int)s->len < out_size - 1 ? (int)s->len : out_size - 1;
    memcpy(out, s->chars, (size_t)len);
    out[len] = '\0';
    return true;
}

static void normalize_pipe_fs_path(const char* path, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    if (!path || !path[0]) return;
#ifdef _WIN32
    snprintf(out, (size_t)out_size, "%s", path);
#else
    char joined[4096];
    if (path[0] == '/') {
        snprintf(joined, sizeof(joined), "%s", path);
    } else {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd))) {
            snprintf(out, (size_t)out_size, "%s", path);
            return;
        }
        snprintf(joined, sizeof(joined), "%s/%s", cwd, path);
    }

    const char* slash = strrchr(joined, '/');
    if (slash && slash[1] != '\0') {
        char parent[4096];
        int parent_len = (int)(slash - joined);
        if (parent_len <= 0) {
            parent[0] = '/';
            parent[1] = '\0';
        } else {
            if (parent_len >= (int)sizeof(parent)) parent_len = (int)sizeof(parent) - 1;
            memcpy(parent, joined, (size_t)parent_len);
            parent[parent_len] = '\0';
        }

        char resolved_parent[4096];
        if (realpath(parent, resolved_parent)) {
            // Node common.PIPE can be relative through a harness symlink; libuv
            // counts raw ".." segments against sockaddr_un before the OS resolves them.
            snprintf(out, (size_t)out_size, "%s/%s", resolved_parent, slash + 1);
            return;
        }
    }
    // libuv pipe connect/bind is resolved against native cwd; normalizing keeps
    // relative Node test pipe paths stable across primary/worker IPC turns.
    snprintf(out, (size_t)out_size, "%s", joined);
#endif
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

static JsBoundSocket* bound_socket_from_item(Item self) {
    if (!net_is_object_like(self)) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__bound_socket_handle__"));
    return (JsBoundSocket*)net_native_ptr_from_item(handle_item);
}

static Item bound_socket_throw_adopted(void) {
    return js_throw_error_with_code("ERR_SOCKET_HANDLE_ADOPTED",
                                    "Socket handle has been adopted");
}

static void bound_socket_free_after_close_cb(uv_handle_t* handle) {
    JsBoundSocket* bound = (JsBoundSocket*)handle->data;
    if (bound) mem_free(bound);
}

static void bound_socket_close_handle(JsBoundSocket* bound,
                                      bool clear_js_handle,
                                      bool free_after_close) {
    if (!bound || bound->closed) return;
    bound->closed = true;
    if (clear_js_handle && bound->js_object.item) {
        js_property_set(bound->js_object, make_string_item("__bound_socket_handle__"),
                        make_undefined_item());
    }
    if (!uv_is_closing((uv_handle_t*)&bound->tcp)) {
        // The bound fd belongs to libuv; closing the uv handle avoids leaving a
        // live handle around a manually closed descriptor after close/adoption.
        uv_close((uv_handle_t*)&bound->tcp,
                 free_after_close ? bound_socket_free_after_close_cb : NULL);
    }
}

static int bound_socket_dup_fd(JsBoundSocket* bound) {
    if (!bound || bound->closed || bound->adopted) return -1;
    uv_os_fd_t fd;
    if (uv_fileno((const uv_handle_t*)&bound->tcp, &fd) != 0) return -1;
#ifdef _WIN32
    return -1;
#else
    int dup_fd = dup((int)fd);
    if (dup_fd >= 0) {
        bound->adopted = true;
        bound_socket_close_handle(bound, false, false);
    }
    return dup_fd;
#endif
}

static Item js_bound_socket_address(void) {
    Item self = js_get_this();
    JsBoundSocket* bound = bound_socket_from_item(self);
    if (!bound) return ItemNull;
    if (bound->adopted) return bound_socket_throw_adopted();
    if (bound->closed) return ItemNull;

    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    int r = uv_tcp_getsockname(&bound->tcp, (struct sockaddr*)&addr, &addrlen);
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

static Item js_bound_socket_fd(void) {
    Item self = js_get_this();
    JsBoundSocket* bound = bound_socket_from_item(self);
    if (!bound) return (Item){.item = i2it(-1)};
    if (bound->adopted) return bound_socket_throw_adopted();
    if (bound->closed) return (Item){.item = i2it(-1)};

    uv_os_fd_t fd;
    if (uv_fileno((const uv_handle_t*)&bound->tcp, &fd) != 0) return (Item){.item = i2it(-1)};
    return (Item){.item = i2it((int64_t)fd)};
}

static Item js_bound_socket_close(void) {
    Item self = js_get_this();
    JsBoundSocket* bound = bound_socket_from_item(self);
    if (!bound) return make_undefined_item();
    if (bound->adopted) return bound_socket_throw_adopted();
    bound_socket_close_handle(bound, true, true);
    return make_undefined_item();
}

static bool bound_socket_has_live_listener(const struct sockaddr* addr, int addrlen) {
    if (!addr || addrlen <= 0) return false;
#ifdef _WIN32
    return false;
#else
    int fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int r = connect(fd, addr, (socklen_t)addrlen);
    close(fd);
    return r == 0;
#endif
}

extern "C" Item js_net_BoundSocket(Item options) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return ItemNull;

    if (!is_undefined_item(options) && options.item != ITEM_NULL && !net_is_object_like(options)) {
        js_throw_invalid_arg_type("options", "Object", options);
        return ItemNull;
    }

    int port = 0;
    char host_buf[256] = "0.0.0.0";
    bool ipv6_only = false;
    bool reuse_port = false;

    if (net_is_object_like(options)) {
        Item host = js_property_get(options, make_string_item("host"));
        if (!is_undefined_item(host) && host.item != ITEM_NULL) {
            if (!copy_string_item(host, host_buf, (int)sizeof(host_buf))) {
                js_throw_invalid_arg_type("options.host", "string", host);
                return ItemNull;
            }
            if (!validate_host_string(host, "options.host")) return ItemNull;
        }
        Item port_item = js_property_get(options, make_string_item("port"));
        if (!is_undefined_item(port_item) && port_item.item != ITEM_NULL) {
            if (!parse_port(port_item, &port)) return ItemNull;
        }
        Item opt_ipv6_only = js_property_get(options, make_string_item("ipv6Only"));
        ipv6_only = get_type_id(opt_ipv6_only) == LMD_TYPE_BOOL && it2b(opt_ipv6_only);
        Item opt_reuse_port = js_property_get(options, make_string_item("reusePort"));
        reuse_port = get_type_id(opt_reuse_port) == LMD_TYPE_BOOL && it2b(opt_reuse_port);
        if ((is_undefined_item(host) || host.item == ITEM_NULL) && ipv6_only) {
            memcpy(host_buf, "::", 3);
        }
    }

    struct sockaddr_storage addr;
    int flags = 0;
#ifdef UV_TCP_REUSEPORT
    if (reuse_port) flags |= UV_TCP_REUSEPORT;
#else
    if (reuse_port) {
        js_throw_error_with_code("ENOSYS", "reusePort is not supported");
        return ItemNull;
    }
#endif

    int addrlen = 0;
    int r = uv_ip4_addr(host_buf, port, (struct sockaddr_in*)&addr);
    if (r != 0) {
        r = uv_ip6_addr(host_buf, port, (struct sockaddr_in6*)&addr);
        if (r == 0 && ipv6_only) flags |= UV_TCP_IPV6ONLY;
        if (r == 0) addrlen = sizeof(struct sockaddr_in6);
    } else {
        addrlen = sizeof(struct sockaddr_in);
    }
    if (r != 0) {
        js_throw_type_error_code("ERR_INVALID_ARG_VALUE", "The property 'options.host' is invalid");
        return ItemNull;
    }
    if (!reuse_port && port > 0 &&
        bound_socket_has_live_listener((const struct sockaddr*)&addr, addrlen)) {
        Item err = make_uv_error(UV_EADDRINUSE, "bind", host_buf, port);
        js_throw_value(err);
        return ItemNull;
    }

    JsBoundSocket* bound = (JsBoundSocket*)mem_calloc(1, sizeof(JsBoundSocket), MEM_CAT_JS_RUNTIME);
    r = uv_tcp_init(loop, &bound->tcp);
    if (r != 0) {
        mem_free(bound);
        return js_throw_error_with_code(uv_err_name(r), uv_strerror(r));
    }
    bound->tcp.data = bound;
    uv_unref((uv_handle_t*)&bound->tcp);

    r = uv_tcp_bind(&bound->tcp, (const struct sockaddr*)&addr, (unsigned int)flags);
    if (r != 0) {
        Item err = make_uv_error(r, "bind", host_buf, port);
        bound_socket_close_handle(bound, false, true);
        js_throw_value(err);
        return ItemNull;
    }

    Item obj = js_new_object();
    js_property_set(obj, make_string_item("__bound_socket_handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)bound)});
    js_property_set(obj, make_string_item("address"),
                    js_new_function((void*)js_bound_socket_address, 0));
    js_property_set(obj, make_string_item("fd"),
                    js_new_function((void*)js_bound_socket_fd, 0));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_bound_socket_close, 0));
    bound->js_object = obj;
    return obj;
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
    return (NetBlockList*)net_native_ptr_from_item(handle_item);
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

static bool net_parse_auto_select_timeout(Item value, const char* name, int* out_timeout) {
    if (!out_timeout) return false;
    TypeId type = get_type_id(value);
    double d = 0.0;
    if (type == LMD_TYPE_INT) {
        d = (double)it2i(value);
    } else if (type == LMD_TYPE_INT64) {
        d = (double)it2l(value);
    } else if (type == LMD_TYPE_FLOAT) {
        d = it2d(value);
    } else {
        js_throw_invalid_arg_type(name, "number", value);
        return false;
    }
    if (d != d || d == 1.0 / 0.0 || d == -1.0 / 0.0 || d <= 0) {
        js_throw_out_of_range(name, ">= 1", value);
        return false;
    }
    *out_timeout = d < 10.0 ? 10 : (int)d;
    return true;
}

static bool net_string_starts_with(Item value, const char* prefix) {
    if (get_type_id(value) != LMD_TYPE_STRING || !prefix) return false;
    String* s = it2s(value);
    size_t len = strlen(prefix);
    return s->len >= (int64_t)len && memcmp(s->chars, prefix, len) == 0;
}

static void net_apply_cli_options(void) {
    if (net_cli_options_applied) return;
    net_cli_options_applied = true;

    Item exec_argv = js_get_process_exec_argv();
    if (get_type_id(exec_argv) != LMD_TYPE_ARRAY) return;

    const char* timeout_prefix = "--network-family-autoselection-attempt-timeout=";
    int64_t len = js_array_length(exec_argv);
    for (int64_t i = 0; i < len; i++) {
        Item arg = js_array_get_int(exec_argv, i);
        if (net_string_starts_with(arg, "--no-network-family-autoselection")) {
            net_default_auto_select_family = false;
        } else if (net_string_starts_with(arg, timeout_prefix)) {
            String* s = it2s(arg);
            const char* start = s->chars + strlen(timeout_prefix);
            char* end = NULL;
            long timeout = strtol(start, &end, 10);
            if (end && end > start && end == s->chars + s->len && timeout > 0) {
                Item timeout_item = (Item){.item = i2it(timeout)};
                int parsed_timeout = 0;
                if (net_parse_auto_select_timeout(timeout_item,
                        "network-family-autoselection-attempt-timeout", &parsed_timeout)) {
                    net_auto_select_family_timeout = parsed_timeout;
                }
            }
        }
    }
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
    if (!is_undefined_item(path) && path.item != ITEM_NULL) {
        if (!copy_string_item(path, out->path, (int)sizeof(out->path))) {
            js_throw_invalid_arg_type("options.path", "string", path);
            return false;
        }
        out->has_path = true;
        return true;
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
        struct sockaddr_in local4;
        struct sockaddr_in6 local6;
        if (uv_ip4_addr(out->local_address, 0, &local4) != 0 &&
            uv_ip6_addr(out->local_address, 0, &local6) != 0) {
            throw_invalid_ip_address(local_address);
            return false;
        }
        out->has_local_address = true;
    }

    Item local_port = js_property_get(options, make_string_item("localPort"));
    if (!is_undefined_item(local_port) && local_port.item != ITEM_NULL) {
        if (!parse_local_port(local_port, &out->local_port)) return false;
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
    } else if (!is_undefined_item(lookup) && lookup.item != ITEM_NULL) {
        js_throw_invalid_arg_type("options.lookup", "Function", lookup);
        return false;
    }

    Item block_list = js_property_get(options, make_string_item("blockList"));
    if (!is_undefined_item(block_list) && block_list.item != ITEM_NULL) {
        out->block_list = block_list;
        out->has_block_list = net_block_list_from_item(block_list) != NULL;
    }

    Item auto_select = js_property_get(options, make_string_item("autoSelectFamily"));
    if (!is_undefined_item(auto_select) && auto_select.item != ITEM_NULL &&
        get_type_id(auto_select) != LMD_TYPE_BOOL) {
        js_throw_invalid_arg_type("options.autoSelectFamily", "boolean", auto_select);
        return false;
    }
    if (get_type_id(auto_select) == LMD_TYPE_BOOL) {
        out->auto_select_family = it2b(auto_select);
        out->auto_select_family_set = true;
    }

    Item auto_select_timeout = js_property_get(options, make_string_item("autoSelectFamilyAttemptTimeout"));
    if (!is_undefined_item(auto_select_timeout) && auto_select_timeout.item != ITEM_NULL) {
        int parsed_timeout = 0;
        if (!net_parse_auto_select_timeout(auto_select_timeout,
                "options.autoSelectFamilyAttemptTimeout", &parsed_timeout)) {
            return false;
        }
        out->auto_select_family_attempt_timeout = parsed_timeout;
    }
    return true;
}

static bool normalize_connect_args(Item rest_args, NetConnectOptions* out) {
    out->port = 0;
    out->family = 0;
    out->host[0] = '\0';
    memcpy(out->host, "127.0.0.1", 10);
    out->has_path = false;
    out->path[0] = '\0';
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
    out->auto_select_family_attempt_timeout = 0;
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

    TypeId first_type = get_type_id(first);
    if (first_type == LMD_TYPE_MAP || first_type == LMD_TYPE_OBJECT ||
        first_type == LMD_TYPE_VMAP || first_type == LMD_TYPE_ELEMENT) {
        if (!normalize_options_object(first, out)) return false;
        if (argc > 1) {
            Item cb = js_array_get_int(rest_args, 1);
            if (is_callable(cb)) out->callback = cb;
        }
        return true;
    }

    if (get_type_id(first) == LMD_TYPE_STRING) {
        String* s = it2s(first);
        bool looks_like_path = s->len > 0 && s->chars[0] == '/';
        for (uint64_t i = 0; i < s->len && !looks_like_path; i++) {
            looks_like_path = s->chars[i] == '/' || s->chars[i] == '\\';
        }
        if (looks_like_path) {
            // Node treats string connect arguments with path separators as IPC
            // paths; parsing relative socket paths as ports leaves pipe tests idle.
            if (!copy_string_item(first, out->path, (int)sizeof(out->path))) return false;
            out->has_path = true;
            return true;
        }
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

static Item socket_auto_errors_array(JsSocket* sock, bool create) {
    if (!sock || !sock->js_object.item) return make_undefined_item();
    Item key = make_string_item("__auto_select_errors__");
    Item arr = js_property_get(sock->js_object, key);
    if (get_type_id(arr) != LMD_TYPE_ARRAY && create) {
        arr = js_array_new(0);
        js_property_set(sock->js_object, key, arr);
    }
    return arr;
}

static void socket_record_auto_error(JsSocket* sock, Item err) {
    Item arr = socket_auto_errors_array(sock, true);
    if (get_type_id(arr) == LMD_TYPE_ARRAY) js_array_push(arr, err);
}

static Item socket_auto_select_final_error(JsSocket* sock, Item fallback) {
    Item arr = socket_auto_errors_array(sock, false);
    if (get_type_id(arr) != LMD_TYPE_ARRAY || js_array_length(arr) <= 1) return fallback;
    // auto-select-family reports the whole failed candidate set; surfacing only
    // the last connect error trips Node tests and leaves later socket cleanup stranded.
    return js_new_aggregate_error(arr, make_string_item(""));
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
                if (socket_connect_resolved(sock, (const struct sockaddr*)&addr6) == 0) {
                    socket_start_auto_attempt_timer(sock);
                    return true;
                }
            }
        } else {
            struct sockaddr_in addr4;
            if (uv_ip4_addr(host, sock->connect_port, &addr4) == 0) {
                if (socket_connect_resolved(sock, (const struct sockaddr*)&addr4) == 0) {
                    socket_start_auto_attempt_timer(sock);
                    return true;
                }
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

static bool net_lookup_invalid_family_value(Item value, int* out_family) {
    Item family = value;
    if (get_type_id(value) == LMD_TYPE_MAP || get_type_id(value) == LMD_TYPE_OBJECT ||
        get_type_id(value) == LMD_TYPE_VMAP) {
        family = js_property_get(value, make_string_item("family"));
    }
    if (get_type_id(family) != LMD_TYPE_INT) return false;
    int detected = (int)it2i(family);
    if (detected == 4 || detected == 6) return false;
    if (out_family) *out_family = detected;
    return true;
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

static Item net_connect_lookup_fail_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();

    Item self = env[0];
    Item err = env[1];
    Item hostname = env[2];
    JsSocket* sock = socket_from_object(self);
    if (!sock || sock->destroyed) return make_undefined_item();

    sock->connect_pending = false;
    socket_update_state_properties(sock);
    socket_fail_pending_writes(sock, err);
    Item lookup_args[4] = { err, make_undefined_item(), make_undefined_item(), hostname };
    socket_emit(self, "lookup", lookup_args, 4);
    socket_emit(self, "error", &err, 1);
    socket_close_now(sock);
    return make_undefined_item();
}

static void net_connect_lookup_fail(NetResolveReq* nr, Item err) {
    if (!nr || !nr->sock) return;
    JsSocket* sock = nr->sock;
    Item* env = js_alloc_env(3);
    env[0] = sock->js_object;
    env[1] = err;
    env[2] = make_string_item(nr->host);
    Item fn = js_new_closure((void*)net_connect_lookup_fail_scheduled, 0, env, 3);
    js_next_tick_enqueue(fn);
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
        char raw_addrs[8][INET6_ADDRSTRLEN];
        int raw_families[8];
        bool raw_used[8];
        int raw_count = 0;
        memset(raw_addrs, 0, sizeof(raw_addrs));
        memset(raw_families, 0, sizeof(raw_families));
        memset(raw_used, 0, sizeof(raw_used));
        sock->auto_addr_index = 0;
        sock->auto_addr_count = 0;
        sock->connect_port = nr->port;
        sock->auto_attempt_timeout_ms = nr->auto_select_family_attempt_timeout;
        js_property_set(sock->js_object, make_string_item("__auto_select_errors__"), js_array_new(0));
        for (int i = 0; i < len; i++) {
            Item record = js_array_get_int(value, i);
            int family = 0;
            int invalid_family = 0;
            if (net_lookup_invalid_family_value(record, &invalid_family)) {
                Item lookup_err = make_invalid_address_family_error(invalid_family, nr->host, nr->port);
                net_connect_lookup_fail(nr, lookup_err);
                mem_free(nr);
                return make_undefined_item();
            }
            if (net_copy_lookup_address(record, raw_addrs[raw_count],
                    (int)sizeof(raw_addrs[0]), &family)) {
                raw_families[raw_count] = family;
                raw_count++;
            }
        }
        int next_family = raw_count > 0 ? raw_families[0] : 4;
        while (sock->auto_addr_count < raw_count) {
            int found = -1;
            for (int i = 0; i < raw_count; i++) {
                if (!raw_used[i] && raw_families[i] == next_family) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                for (int i = 0; i < raw_count; i++) {
                    if (!raw_used[i]) {
                        found = i;
                        break;
                    }
                }
            }
            if (found < 0) break;
            // Node alternates resolved families for Happy Eyeballs; preserving
            // DNS order by family lets a batch of one family stall later tries.
            raw_used[found] = true;
            memcpy(sock->auto_addrs[sock->auto_addr_count], raw_addrs[found],
                   sizeof(sock->auto_addrs[0]));
            sock->auto_families[sock->auto_addr_count] = raw_families[found];
            sock->auto_addr_count++;
            next_family = next_family == 6 ? 4 : 6;
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
        int invalid_family = 0;
        if (net_lookup_invalid_family_value(family_item, &invalid_family)) {
            Item lookup_err = make_invalid_address_family_error(invalid_family, nr->host, nr->port);
            net_connect_lookup_fail(nr, lookup_err);
            mem_free(nr);
            return make_undefined_item();
        }
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

    Item lookup_err = make_invalid_ip_address_error(value);
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
    int auto_select_family_attempt_timeout = options->auto_select_family_attempt_timeout > 0 ?
        options->auto_select_family_attempt_timeout : net_auto_select_family_timeout;

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
        nr->auto_select_family_attempt_timeout = auto_select_family_attempt_timeout;
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
    nr->auto_select_family_attempt_timeout = auto_select_family_attempt_timeout;
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
    socket_clear_auto_attempt_timer(sock);
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
        const char* err_host = sock->connect_host[0] ? sock->connect_host : NULL;
        int err_port = err_host ? sock->connect_port : -1;
        Item err = make_uv_error(status, "connect", err_host, err_port);
        if (sock->auto_select_family && sock->auto_addr_count > 0) {
            socket_record_auto_error(sock, err);
        }
        if (socket_schedule_auto_retry(sock)) return;
        if (sock->auto_select_family && sock->auto_addr_count > 0) {
            err = socket_auto_select_final_error(sock, err);
        }
        socket_fail_pending_writes(sock, err);
        socket_emit(sock->js_object, "error", &err, 1);
        socket_close_now(sock);
        return;
    }

    sock->connected = true;
    if (sock->auto_select_family && sock->js_object.item) {
        js_property_set(sock->js_object, make_string_item("__auto_select_errors__"), make_undefined_item());
    }
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

static void pipe_connect_close_cb(uv_handle_t* handle) {
    NetPipeConnectReq* pc = handle ? (NetPipeConnectReq*)handle->data : NULL;
    if (pc) mem_free(pc);
}

static void pipe_connect_cb(uv_connect_t* req, int status) {
    NetPipeConnectReq* pc = req ? (NetPipeConnectReq*)req->data : NULL;
    JsSocket* sock = pc ? pc->sock : NULL;
    if (!pc || !sock) return;

    sock->connect_pending = false;
    if (status != 0) {
        Item err = make_path_connect_error(status, "");
        socket_emit(sock->js_object, "error", &err, 1);
    } else {
        sock->connected = true;
        socket_update_state_properties(sock);
        socket_emit(sock->js_object, "connect", NULL, 0);
        socket_emit(sock->js_object, "ready", NULL, 0);
    }
    socket_close_now(sock);
    if (!uv_is_closing((uv_handle_t*)&pc->pipe)) {
        uv_close((uv_handle_t*)&pc->pipe, pipe_connect_close_cb);
    }
}

static int socket_start_pipe_connect(JsSocket* sock, const char* path) {
    if (!sock || !path || !path[0]) return UV_EINVAL;
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return UV_EINVAL;
    NetPipeConnectReq* pc = (NetPipeConnectReq*)mem_calloc(1, sizeof(NetPipeConnectReq), MEM_CAT_JS_RUNTIME);
    if (!pc) return UV_ENOMEM;
    pc->sock = sock;
    normalize_pipe_fs_path(path, pc->path, (int)sizeof(pc->path));
    pc->req.data = pc;
    pc->pipe.data = pc;
    uv_pipe_init(loop, &pc->pipe, 0);
    sock->connect_pending = true;
    socket_update_state_properties(sock);
    // primary-owned cluster pipe listeners must still use libuv's pipe connect
    // path; synthetic connects corrupt loop ownership during drain.
    uv_pipe_connect(&pc->req, &pc->pipe, pc->path, pipe_connect_cb);
    return 0;
}

static Item create_socket_for_connect(const NetConnectOptions* options) {
    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) {
        log_error("net: createConnection: no event loop");
        return ItemNull;
    }

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    sock->high_water_mark = NET_SOCKET_DEFAULT_HIGH_WATER_MARK;
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    Item obj = make_socket_object(sock, true);
    if (options->allow_half_open) {
        js_property_set(obj, make_string_item("allowHalfOpen"), (Item){.item = ITEM_TRUE});
    }
    socket_sync_no_half_open_listener(obj);
    socket_store_connect_options(sock, options);
    if (options->has_onread) socket_configure_onread(sock, options->onread);
    if (options->has_signal && socket_configure_abort_signal(sock, options->signal)) return obj;
    if (is_callable(options->callback)) socket_set_listener(obj, "connect", options->callback);

    if (!net_permission_allowed()) {
        Item err = make_net_permission_error(options);
        // permission-denied connects must not start lookup/connect handles that
        // would survive until the event-loop drain watchdog.
        socket_schedule_error_close_event(sock, err);
        return obj;
    }

    if (options->has_path) {
        int r = socket_start_pipe_connect(sock, options->path);
        if (r != 0) {
            Item err = make_path_connect_error(r, options->path);
            socket_schedule_error_close_event(sock, err);
        }
        return obj;
    }

    int r = socket_start_connect(sock, options);
    if (r != 0) {
        Item err = make_uv_error(r, "connect", options->host, options->port);
        socket_fail_pending_writes(sock, err);
        socket_schedule_error_close_event(sock, err);
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
    sock->high_water_mark = NET_SOCKET_DEFAULT_HIGH_WATER_MARK;
    sock->js_object = self;
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    js_property_set(self, make_string_item("__handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)sock)});
    js_property_set(self, make_string_item("_handle"), make_socket_handle_object(sock));
    sock->handle_exposed = true;
    // reconnect reuses the JS Socket object; clear the prior peer-FIN marker so new writes are not EPIPE.
    js_property_set(self, make_string_item("__remote_ended__"), make_undefined_item());
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
    if (sock->adopted_bound_socket && (options.has_local_address || options.has_local_port)) {
        return js_throw_type_error_code(
            "ERR_INVALID_ARG_VALUE",
            "The argument 'options' cannot set localAddress or localPort when a bound handle is used");
    }
    if (options.allow_half_open) {
        js_property_set(self, make_string_item("allowHalfOpen"), (Item){.item = ITEM_TRUE});
    }
    socket_sync_no_half_open_listener(self);
    socket_store_connect_options(sock, &options);
    if (options.has_onread) socket_configure_onread(sock, options.onread);
    if (options.has_signal && socket_configure_abort_signal(sock, options.signal)) return self;
    if (is_callable(options.callback)) socket_set_listener(self, "connect", options.callback);

    if (!net_permission_allowed()) {
        Item err = make_net_permission_error(&options);
        // permission-denied connects must not start lookup/connect handles that
        // would survive until the event-loop drain watchdog.
        socket_schedule_error_close_event(sock, err);
        return self;
    }

    if (options.has_path) {
        int r = socket_start_pipe_connect(sock, options.path);
        if (r != 0) {
            Item err = make_path_connect_error(r, options.path);
            socket_schedule_error_close_event(sock, err);
        }
        return self;
    }

    int r = socket_start_connect(sock, &options);
    if (r != 0) {
        Item err = make_uv_error(r, "connect", options.host, options.port);
        socket_fail_pending_writes(sock, err);
        socket_schedule_error_close_event(sock, err);
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
    uv_pipe_t pipe;
    Item     js_object;
    Item     connection_handler;
    bool     is_pipe;
    bool     closed;
    bool     listen_pending;
    bool     allow_half_open;
    bool     close_requested;
    bool     handle_closed;
    bool     close_event_emitted;
    bool     ipc_transfer_defer_close_event;
    bool     has_block_list;
    bool     listen_after_close;
    bool     keep_alive;
    bool     no_delay;
    bool     pause_on_connect;
    int      connection_count;
    int      keep_alive_initial_delay_ms;
    Item     block_list;
    Item     pending_listen_port;
    Item     pending_listen_host;
    Item     pending_listen_callback;
};

#define NET_CLUSTER_PIPE_SERVER_MAX 16
#define NET_CLUSTER_PIPE_WORKER_MAX 32
#define NET_CLUSTER_PIPE_PENDING_MAX 256
#define NET_CLUSTER_PIPE_SOCKET_MAX 512

typedef struct NetClusterPipeWorker {
    Item worker;
    int  max_connections;
    bool active;
} NetClusterPipeWorker;

typedef struct NetClusterPipePending {
    Item handle;
    int64_t seq;
    int selected_worker;
    bool active;
    bool in_flight;
    bool rejected_workers[NET_CLUSTER_PIPE_WORKER_MAX];
} NetClusterPipePending;

typedef struct NetClusterPipeServer {
    uv_pipe_t pipe;
    char path[4096];
    int64_t next_seq;
    bool active;
    bool closing;
    NetClusterPipeWorker workers[NET_CLUSTER_PIPE_WORKER_MAX];
    NetClusterPipePending pending[NET_CLUSTER_PIPE_PENDING_MAX];
} NetClusterPipeServer;

typedef struct NetClusterWorkerPipeServer {
    char path[4096];
    Item server_obj;
    Item callback;
    bool active;
    bool query_sent;
    bool listening_emitted;
} NetClusterWorkerPipeServer;

typedef struct NetClusterPipeSocketRecord {
    Item obj;
    JsServer* owner;
    bool active;
} NetClusterPipeSocketRecord;

static NetClusterPipeServer net_cluster_pipe_servers[NET_CLUSTER_PIPE_SERVER_MAX];
static NetClusterWorkerPipeServer net_cluster_worker_pipe_servers[NET_CLUSTER_PIPE_WORKER_MAX];
static Item net_cluster_pipe_worker_roots[NET_CLUSTER_PIPE_SERVER_MAX][NET_CLUSTER_PIPE_WORKER_MAX];
static Item net_cluster_pipe_pending_roots[NET_CLUSTER_PIPE_SERVER_MAX][NET_CLUSTER_PIPE_PENDING_MAX];
static Item net_cluster_worker_server_roots[NET_CLUSTER_PIPE_WORKER_MAX];
static Item net_cluster_worker_callback_roots[NET_CLUSTER_PIPE_WORKER_MAX];
static Item net_cluster_pipe_socket_roots[NET_CLUSTER_PIPE_SOCKET_MAX];
static NetClusterPipeSocketRecord net_cluster_pipe_sockets[NET_CLUSTER_PIPE_SOCKET_MAX];
static bool net_cluster_pipe_roots_registered = false;

static JsServer* server_from_object(Item self);
static bool server_emit(Item self, const char* event, Item* args, int argc);
static void server_maybe_finish_close(JsServer* srv);
static int server_max_connections(JsServer* srv);
static void net_cluster_close_pipe_sockets_for_server(JsServer* srv);

static void net_cluster_pipe_server_close_cb(uv_handle_t* handle) {
    NetClusterPipeServer* server = handle ? (NetClusterPipeServer*)handle->data : NULL;
    if (!server) return;
    memset(server, 0, sizeof(NetClusterPipeServer));
}

static void net_cluster_pipe_register_roots(void) {
    if (net_cluster_pipe_roots_registered) return;
    heap_register_gc_root_range((uint64_t*)net_cluster_pipe_worker_roots,
        NET_CLUSTER_PIPE_SERVER_MAX * NET_CLUSTER_PIPE_WORKER_MAX);
    heap_register_gc_root_range((uint64_t*)net_cluster_pipe_pending_roots,
        NET_CLUSTER_PIPE_SERVER_MAX * NET_CLUSTER_PIPE_PENDING_MAX);
    heap_register_gc_root_range((uint64_t*)net_cluster_worker_server_roots,
        NET_CLUSTER_PIPE_WORKER_MAX);
    heap_register_gc_root_range((uint64_t*)net_cluster_worker_callback_roots,
        NET_CLUSTER_PIPE_WORKER_MAX);
    heap_register_gc_root_range((uint64_t*)net_cluster_pipe_socket_roots,
        NET_CLUSTER_PIPE_SOCKET_MAX);
    net_cluster_pipe_roots_registered = true;
}

static void net_cluster_close_pipe_handle_item(Item handle) {
    if (!net_is_object_like(handle)) return;
    Item close_fn = js_property_get(handle, make_string_item("close"));
    if (is_callable(close_fn)) {
        js_call_function(close_fn, handle, NULL, 0);
        js_microtask_flush();
    }
}

static int64_t net_cluster_int64_value(Item value, int64_t fallback) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) return it2i(value);
    if (type == LMD_TYPE_INT64) return it2l(value);
    if (type == LMD_TYPE_FLOAT) return (int64_t)it2d(value);
    return fallback;
}

static bool net_cluster_bool_value(Item value) {
    if (value.item == ITEM_TRUE || value.item == b2it(true)) return true;
    if (get_type_id(value) == LMD_TYPE_BOOL) return it2b(value);
    return false;
}

static NetClusterPipePending* net_cluster_primary_find_pending(NetClusterPipeServer* server,
                                                               int64_t seq) {
    if (!server) return NULL;
    for (int i = 0; i < NET_CLUSTER_PIPE_PENDING_MAX; i++) {
        NetClusterPipePending* pending = &server->pending[i];
        if (pending->active && pending->seq == seq) return pending;
    }
    return NULL;
}

static void net_cluster_primary_clear_pending(NetClusterPipeServer* server,
                                              NetClusterPipePending* pending) {
    if (!server || !pending) return;
    int server_index = (int)(server - net_cluster_pipe_servers);
    int pending_index = (int)(pending - server->pending);
    if (pending_index >= 0 && pending_index < NET_CLUSTER_PIPE_PENDING_MAX) {
        net_cluster_pipe_pending_roots[server_index][pending_index] = make_undefined_item();
    }
    memset(pending, 0, sizeof(NetClusterPipePending));
}

static NetClusterPipePending* net_cluster_primary_alloc_pending(NetClusterPipeServer* server,
                                                                Item handle) {
    if (!server || !net_is_object_like(handle)) return NULL;
    net_cluster_pipe_register_roots();
    for (int i = 0; i < NET_CLUSTER_PIPE_PENDING_MAX; i++) {
        NetClusterPipePending* pending = &server->pending[i];
        if (pending->active) continue;
        memset(pending, 0, sizeof(NetClusterPipePending));
        pending->active = true;
        pending->selected_worker = -1;
        pending->handle = handle;
        pending->seq = ++server->next_seq;
        int server_index = (int)(server - net_cluster_pipe_servers);
        net_cluster_pipe_pending_roots[server_index][i] = handle;
        return pending;
    }
    return NULL;
}

static int net_cluster_primary_select_worker(NetClusterPipeServer* server,
                                             NetClusterPipePending* pending) {
    if (!server || !pending) return -1;
    for (int w = 0; w < NET_CLUSTER_PIPE_WORKER_MAX; w++) {
        NetClusterPipeWorker* candidate = &server->workers[w];
        if (!candidate->active || !candidate->worker.item) continue;
        if (pending->rejected_workers[w]) continue;
        return w;
    }
    return -1;
}

static bool net_cluster_primary_send_pending(NetClusterPipeServer* server,
                                             NetClusterPipePending* pending) {
    if (!server || !server->active || server->closing || !pending || !pending->active) {
        return false;
    }
    int worker_index = net_cluster_primary_select_worker(server, pending);
    if (worker_index < 0) return false;
    NetClusterPipeWorker* selected = &server->workers[worker_index];

    Item message = js_new_object();
    js_property_set(message, make_string_item("act"), make_string_item("newconn"));
    js_property_set(message, make_string_item("__lambda_cluster_pipe_newconn__"),
                    (Item){.item = ITEM_TRUE});
    js_property_set(message, make_string_item("key"), make_string_item(server->path));
    js_property_set(message, make_string_item("path"), make_string_item(server->path));
    js_property_set(message, make_string_item("seq"), (Item){.item = i2it(pending->seq)});
    Item send = js_property_get(selected->worker, make_string_item("send"));
    bool sent = false;
    if (is_callable(send)) {
        Item options = js_new_object();
        js_property_set(options, make_string_item("keepOpen"), (Item){.item = ITEM_TRUE});
        Item args[3] = { message, pending->handle, options };
        Item result = js_call_function(send, selected->worker, args, 3);
        sent = result.item == ITEM_TRUE || result.item == b2it(true);
        js_microtask_flush();
    }
    if (!sent) {
        pending->rejected_workers[worker_index] = true;
        return net_cluster_primary_send_pending(server, pending);
    }
    pending->selected_worker = worker_index;
    pending->in_flight = true;
    return sent;
}

static void net_cluster_primary_close_pipe_server(NetClusterPipeServer* server) {
    if (!server || (!server->active && !server->closing)) return;
    int server_index = (int)(server - net_cluster_pipe_servers);
    for (int i = 0; i < NET_CLUSTER_PIPE_PENDING_MAX; i++) {
        NetClusterPipePending* pending = &server->pending[i];
        if (pending->active) {
            net_cluster_close_pipe_handle_item(pending->handle);
            net_cluster_pipe_pending_roots[server_index][i] = make_undefined_item();
            memset(pending, 0, sizeof(NetClusterPipePending));
        }
    }
    for (int i = 0; i < NET_CLUSTER_PIPE_WORKER_MAX; i++) {
        server->workers[i].active = false;
        server->workers[i].worker = make_undefined_item();
        net_cluster_pipe_worker_roots[server_index][i] = make_undefined_item();
    }
    server->active = false;
    if (!server->closing) {
        server->closing = true;
        if (!uv_is_closing((uv_handle_t*)&server->pipe)) {
            uv_close((uv_handle_t*)&server->pipe, net_cluster_pipe_server_close_cb);
        }
    }
}

extern "C" void js_net_cluster_primary_worker_disconnected(Item worker) {
    if (!net_is_object_like(worker)) return;
    for (int s = 0; s < NET_CLUSTER_PIPE_SERVER_MAX; s++) {
        NetClusterPipeServer* server = &net_cluster_pipe_servers[s];
        if (!server->active || server->closing) continue;
        bool removed = false;
        for (int w = 0; w < NET_CLUSTER_PIPE_WORKER_MAX; w++) {
            NetClusterPipeWorker* entry = &server->workers[w];
            if (!entry->active || entry->worker.item != worker.item) continue;
            entry->active = false;
            entry->worker = make_undefined_item();
            net_cluster_pipe_worker_roots[s][w] = make_undefined_item();
            removed = true;
        }
        if (!removed) continue;

        bool has_workers = false;
        for (int w = 0; w < NET_CLUSTER_PIPE_WORKER_MAX; w++) {
            if (server->workers[w].active && server->workers[w].worker.item) {
                has_workers = true;
                break;
            }
        }
        if (!has_workers) {
            // primary-owned RR pipe listeners outlive worker IPC unless the last
            // disconnect closes the shared accept handle and its pending tokens.
            net_cluster_primary_close_pipe_server(server);
            continue;
        }

        for (int p = 0; p < NET_CLUSTER_PIPE_PENDING_MAX; p++) {
            NetClusterPipePending* pending = &server->pending[p];
            if (!pending->active || pending->selected_worker < 0 ||
                pending->selected_worker >= NET_CLUSTER_PIPE_WORKER_MAX) {
                continue;
            }
            NetClusterPipeWorker* selected = &server->workers[pending->selected_worker];
            if (selected->active && selected->worker.item) continue;
            pending->rejected_workers[pending->selected_worker] = true;
            pending->selected_worker = -1;
            pending->in_flight = false;
            if (!net_cluster_primary_send_pending(server, pending)) {
                net_cluster_close_pipe_handle_item(pending->handle);
                net_cluster_primary_clear_pending(server, pending);
            }
        }
    }
}

static bool net_cluster_primary_dispatch_pipe_connection(NetClusterPipeServer* server, Item handle) {
    // primary-owned RR pipes cannot count sends as accepts; workers decide from
    // live maxConnections and ack so rejected descriptors can be rerouted.
    NetClusterPipePending* pending = net_cluster_primary_alloc_pending(server, handle);
    if (!pending) return false;
    if (net_cluster_primary_send_pending(server, pending)) return true;
    net_cluster_primary_clear_pending(server, pending);
    return false;
}

static NetClusterPipeSocketRecord* net_cluster_pipe_socket_record_from_object(Item obj) {
    if (!net_is_object_like(obj)) return NULL;
    Item ptr = js_property_get(obj, make_string_item("__cluster_pipe_socket_record__"));
    return (NetClusterPipeSocketRecord*)net_native_ptr_from_item(ptr);
}

static void net_cluster_close_pipe_socket_record(NetClusterPipeSocketRecord* record) {
    if (!record || !record->active) return;
    Item obj = record->obj;
    JsServer* owner = record->owner;
    record->active = false;
    record->obj = make_undefined_item();
    record->owner = NULL;
    if (net_is_object_like(obj)) {
        js_property_set(obj, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
        Item handle = js_property_get(obj, make_string_item("_handle"));
        js_property_set(obj, make_string_item("_handle"), ItemNull);
        js_property_set(obj, make_string_item("__cluster_pipe_socket_record__"),
                        make_undefined_item());
        net_cluster_close_pipe_handle_item(handle);
    }
    if (owner && owner->connection_count > 0) {
        owner->connection_count--;
        server_maybe_finish_close(owner);
    }
}

static void net_cluster_close_pipe_sockets_for_server(JsServer* srv) {
    if (!srv) return;
    for (int i = 0; i < NET_CLUSTER_PIPE_SOCKET_MAX; i++) {
        NetClusterPipeSocketRecord* record = &net_cluster_pipe_sockets[i];
        if (record->active && record->owner == srv) {
            net_cluster_close_pipe_socket_record(record);
            net_cluster_pipe_socket_roots[i] = make_undefined_item();
        }
    }
}

static Item js_cluster_pipe_socket_destroy(void) {
    Item self = js_get_this();
    NetClusterPipeSocketRecord* record = net_cluster_pipe_socket_record_from_object(self);
    if (record) {
        // worker pipe sockets are accounting tokens for primary-transferred
        // descriptors; maxConnections must drop only when the socket closes.
        net_cluster_close_pipe_socket_record(record);
        int index = (int)(record - net_cluster_pipe_sockets);
        if (index >= 0 && index < NET_CLUSTER_PIPE_SOCKET_MAX) {
            net_cluster_pipe_socket_roots[index] = make_undefined_item();
        }
        return make_undefined_item();
    }
    Item handle = js_property_get(self, make_string_item("_handle"));
    net_cluster_close_pipe_handle_item(handle);
    js_property_set(self, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
    return make_undefined_item();
}

static Item make_cluster_pipe_socket_object(Item handle, JsServer* owner) {
    net_cluster_pipe_register_roots();
    Item obj = js_new_object();
    js_property_set(obj, make_string_item("_handle"), net_is_object_like(handle) ? handle : ItemNull);
    js_property_set(obj, make_string_item("destroy"),
                    js_new_function((void*)js_cluster_pipe_socket_destroy, 0));
    js_property_set(obj, make_string_item("end"),
                    js_new_function((void*)js_cluster_pipe_socket_destroy, 0));
    js_property_set(obj, make_string_item("readable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("writable"), (Item){.item = ITEM_TRUE});
    js_property_set(obj, make_string_item("destroyed"), (Item){.item = ITEM_FALSE});
    for (int i = 0; i < NET_CLUSTER_PIPE_SOCKET_MAX; i++) {
        NetClusterPipeSocketRecord* record = &net_cluster_pipe_sockets[i];
        if (record->active) continue;
        record->active = true;
        record->obj = obj;
        record->owner = owner;
        net_cluster_pipe_socket_roots[i] = obj;
        js_property_set(obj, make_string_item("__cluster_pipe_socket_record__"),
                        (Item){.item = i2it((int64_t)(uintptr_t)record)});
        break;
    }
    return obj;
}

static uv_handle_t* server_handle(JsServer* srv) {
    if (!srv) return NULL;
    return srv->is_pipe ? (uv_handle_t*)&srv->pipe : (uv_handle_t*)&srv->tcp;
}

static Item server_make_listener_record(Item listener, bool once) {
    Item record = js_new_object();
    js_property_set(record, make_string_item("listener"), listener);
    js_property_set(record, make_string_item("once"), (Item){.item = b2it(once)});
    return record;
}

static Item server_listener_fn(Item record) {
    if (get_type_id(record) == LMD_TYPE_MAP || get_type_id(record) == LMD_TYPE_OBJECT ||
        get_type_id(record) == LMD_TYPE_VMAP) {
        return js_property_get(record, make_string_item("listener"));
    }
    return record;
}

static bool server_listener_once(Item record) {
    if (get_type_id(record) != LMD_TYPE_MAP && get_type_id(record) != LMD_TYPE_OBJECT &&
        get_type_id(record) != LMD_TYPE_VMAP) {
        return false;
    }
    Item once = js_property_get(record, make_string_item("once"));
    return get_type_id(once) == LMD_TYPE_BOOL && it2b(once);
}

static Item server_listener_map(Item self, bool create) {
    Item listeners = js_property_get(self, make_string_item("__server_listeners__"));
    if (get_type_id(listeners) != LMD_TYPE_MAP && get_type_id(listeners) != LMD_TYPE_OBJECT &&
        get_type_id(listeners) != LMD_TYPE_VMAP) {
        if (!create) return make_undefined_item();
        listeners = js_new_object();
        js_property_set(self, make_string_item("__server_listeners__"), listeners);
    }
    return listeners;
}

static void server_add_listener(Item self, Item event_item, Item callback, bool once) {
    if (get_type_id(event_item) != LMD_TYPE_STRING || !is_callable(callback)) return;
    Item listeners = server_listener_map(self, true);
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        arr = js_array_new(0);
        js_property_set(listeners, event_item, arr);
    }
    js_array_push(arr, server_make_listener_record(callback, once));
}

static bool server_emit(Item self, const char* event, Item* args, int argc) {
    Item listeners = server_listener_map(self, false);
    if (get_type_id(listeners) != LMD_TYPE_MAP && get_type_id(listeners) != LMD_TYPE_OBJECT &&
        get_type_id(listeners) != LMD_TYPE_VMAP) {
        return false;
    }

    Item event_item = make_string_item(event);
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return false;

    int64_t len = js_array_length(arr);
    if (len <= 0) return false;
    Item next = js_array_new(0);
    bool emitted = false;
    for (int64_t i = 0; i < len; i++) {
        Item record = js_array_get_int(arr, i);
        Item callback = server_listener_fn(record);
        bool once = server_listener_once(record);
        if (is_callable(callback)) {
            emitted = true;
            js_call_function(callback, self, args, argc);
            js_microtask_flush();
        }
        if (!once) js_array_push(next, record);
    }
    js_property_set(listeners, event_item, next);
    return emitted;
}

static void server_maybe_finish_close(JsServer* srv) {
    if (!srv || !srv->close_requested || !srv->handle_closed) return;
    if (srv->connection_count > 0) return;

    // the listening handle can close before accepted sockets finish closing.
    // keep JsServer alive until all owner_server links are detached, or socket
    // close callbacks can decrement connection_count through a freed pointer.
    net_active_remove(net_active_servers, NET_ACTIVE_SERVER_MAX, srv->js_object);
    if (srv->ipc_transfer_defer_close_event) return;
    if (!srv->close_event_emitted) {
        srv->close_event_emitted = true;
        js_property_set(srv->js_object, make_string_item("listening"), (Item){.item = ITEM_FALSE});
        server_emit(srv->js_object, "close", NULL, 0);
        Item close_callback = js_property_get(srv->js_object, make_string_item("__close_callback__"));
        if (is_callable(close_callback)) {
            js_call_function(close_callback, srv->js_object, NULL, 0);
            js_property_set(srv->js_object, make_string_item("__close_callback__"), make_undefined_item());
        }
    }
    if (srv->listen_after_close) {
        srv->listen_after_close = false;
        Item args[3] = {
            srv->pending_listen_port,
            srv->pending_listen_host,
            srv->pending_listen_callback
        };
        srv->pending_listen_port = make_undefined_item();
        srv->pending_listen_host = make_undefined_item();
        srv->pending_listen_callback = make_undefined_item();
        Item listen_fn = js_property_get(srv->js_object, make_string_item("listen"));
        if (is_callable(listen_fn)) {
            js_call_function(listen_fn, srv->js_object, args, 3);
            js_microtask_flush();
        }
    }
}

static void server_close_handle_now(JsServer* srv) {
    if (!srv) return;
    srv->closed = true;
    srv->close_requested = true;
    srv->listen_pending = false;
    net_cluster_close_pipe_sockets_for_server(srv);
    uv_handle_t* handle = server_handle(srv);
    if (handle && !uv_is_closing(handle)) {
        uv_close(handle, [](uv_handle_t* h) {
            JsServer* s = h ? (JsServer*)h->data : NULL;
            if (s) {
                s->handle_closed = true;
                server_maybe_finish_close(s);
            }
        });
    } else if (srv->handle_closed) {
        server_maybe_finish_close(srv);
    }
}

static void server_close_after_listen_error(JsServer* srv) {
    // Failed bind/listen still leaves an initialized TCP handle; close it so
    // error-only servers do not keep cluster workers alive until the watchdog.
    server_close_handle_now(srv);
}

static void socket_note_closed(JsSocket* sock) {
    if (!sock || !sock->owner_server) return;
    JsServer* srv = sock->owner_server;
    if (sock->transfer_account_pending) {
        // keepOpen:false IPC transfer closes the sender fd here, but server
        // connection accounting belongs to the receiver until its socket closes.
        sock->transfer_account_pending = false;
    } else if (srv->connection_count > 0) {
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
    if (get_type_id(max_item) == LMD_TYPE_FLOAT) return (int)it2d(max_item);
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
    Item data = server_make_drop_data(client);
    server_emit(srv->js_object, "drop", &data, 1);
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

static Item make_server_handle_object(JsServer* srv);

static bool server_emit_cluster_worker_newconn(JsServer* srv, JsSocket* client, Item client_handle) {
    if (!srv || !client || !js_cluster_is_worker_runtime()) return false;
    Item message = js_new_object();
    js_property_set(message, make_string_item("act"), make_string_item("newconn"));
    // Cluster SCHED_RR delivers accepted sockets through internalMessage before
    // public connection handlers; worker tests may replace handle.close there.
    js_process_emit2(make_string_item("internalMessage"), message, client_handle);
    js_microtask_flush();
    if (js_check_exception()) return false;

    bool listener_closed_server = srv->closed || srv->close_requested;
    bool listener_closed_handle = uv_is_closing((uv_handle_t*)&client->tcp);
    if (!listener_closed_server && !listener_closed_handle) return false;
    if (!listener_closed_handle) {
        Item close_fn = js_property_get(client_handle, make_string_item("close"));
        if (is_callable(close_fn)) {
            js_call_function(close_fn, client_handle, NULL, 0);
            js_microtask_flush();
        } else {
            socket_close_now(client);
        }
    }
    return true;
}

static JsServer* server_from_handle_object(Item self) {
    if (!net_is_object_like(self)) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__server_handle__"));
    // Server handle wrappers store native pointers; worker-mode values can be
    // boxed as INT64, so decode both integer representations.
    return (JsServer*)net_native_ptr_from_item(handle_item);
}

static void server_apply_accepted_handle_options(JsServer* srv, JsSocket* client, Item client_handle) {
    if (!srv || !client || !net_is_object_like(client_handle)) return;
    if (srv->keep_alive) {
        client->keep_alive_requested = true;
        client->keep_alive_delay_secs = srv->keep_alive_initial_delay_ms > 0
            ? srv->keep_alive_initial_delay_ms / 1000
            : 0;
        Item fn = js_property_get(client_handle, make_string_item("setKeepAlive"));
        if (is_callable(fn)) {
            Item args[2] = {
                (Item){.item = ITEM_TRUE},
                (Item){.item = i2it(srv->keep_alive_initial_delay_ms)}
            };
            js_call_function(fn, client_handle, args, 2);
            js_microtask_flush();
        }
    }
    if (srv->no_delay) {
        client->no_delay_requested = true;
        Item fn = js_property_get(client_handle, make_string_item("setNoDelay"));
        if (is_callable(fn)) {
            Item arg = (Item){.item = ITEM_TRUE};
            js_call_function(fn, client_handle, &arg, 1);
            js_microtask_flush();
        }
    }
}

static Item server_accept_client(JsServer* srv, JsSocket* client, Item client_handle) {
    if (!srv || !client) return make_undefined_item();
    if (client->js_object.item) return make_undefined_item();

    server_apply_accepted_handle_options(srv, client, client_handle);

    Item client_obj = make_socket_object(client, false);
    if (net_is_object_like(client_handle)) {
        js_property_set(client_obj, make_string_item("_handle"), client_handle);
        client->handle_exposed = true;
    } else {
        socket_expose_handle(client);
    }
    client->connected = true;
    client->is_server_side = true;
    client->owner_server = srv;
    client->paused = srv->pause_on_connect;
    js_property_set(client_obj, make_string_item("allowHalfOpen"),
                    (Item){.item = b2it(srv->allow_half_open)});
    socket_sync_no_half_open_listener(client_obj);
    srv->connection_count++;
    socket_update_state_properties(client);
    socket_update_address_properties(client);

    if (get_type_id(srv->connection_handler) == LMD_TYPE_FUNC) {
        Item result = js_call_function(srv->connection_handler, srv->js_object, &client_obj, 1);
        server_capture_connection_rejection(result, client_obj);
        js_microtask_flush();
    }

    server_emit(srv->js_object, "connection", &client_obj, 1);

    if (!client->paused) {
        socket_start_read(client);
    }
    return make_undefined_item();
}

static Item js_server_handle_onconnection(Item err_item, Item client_handle) {
    (void)err_item;
    Item self = js_get_this();
    JsServer* srv = server_from_handle_object(self);
    JsSocket* client = socket_from_handle_object(client_handle);
    return server_accept_client(srv, client, client_handle);
}

static Item make_server_handle_object(JsServer* srv) {
    Item handle = js_new_object();
    js_property_set(handle, make_string_item("__server_handle__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)srv)});
    js_property_set(handle, make_string_item("onconnection"),
                    js_new_function((void*)js_server_handle_onconnection, 2));
    return handle;
}

static int net_dup_uv_fd(const uv_handle_t* handle) {
#ifdef _WIN32
    (void)handle;
    return -1;
#else
    if (!handle) return -1;
    uv_os_fd_t fd;
    if (uv_fileno(handle, &fd) != 0) return -1;
    return dup((int)fd);
#endif
}

extern "C" int js_net_dup_ipc_stdio_fd(Item handle_item) {
    if (!net_is_object_like(handle_item)) return -1;
    JsServer* srv = server_from_handle_object(handle_item);
    if (srv && !uv_is_closing((uv_handle_t*)&srv->tcp)) {
        return net_dup_uv_fd((const uv_handle_t*)&srv->tcp);
    }
    JsSocket* sock = socket_from_handle_object(handle_item);
    if (sock && !uv_is_closing((uv_handle_t*)&sock->tcp)) {
        return net_dup_uv_fd((const uv_handle_t*)&sock->tcp);
    }

    Item inner = js_property_get(handle_item, make_string_item("_handle"));
    if (net_is_object_like(inner) && inner.item != handle_item.item) {
        // node stdio arrays receive public socket/server objects as well as
        // their internal _handle objects; unwrap once to preserve fd ownership.
        return js_net_dup_ipc_stdio_fd(inner);
    }
    return -1;
}

extern "C" uv_stream_t* js_net_stream_from_ipc_send_handle(Item handle_item) {
    if (!net_is_object_like(handle_item)) return NULL;
    JsPipeHandle* ph = pipe_handle_from_object(handle_item);
    if (ph && !ph->closed && !uv_is_closing((uv_handle_t*)&ph->pipe)) {
        return (uv_stream_t*)&ph->pipe;
    }
    JsServer* srv = server_from_handle_object(handle_item);
    if (srv && !uv_is_closing((uv_handle_t*)&srv->tcp)) {
        return (uv_stream_t*)&srv->tcp;
    }
    JsSocket* sock = socket_from_handle_object(handle_item);
    if (sock && !uv_is_closing((uv_handle_t*)&sock->tcp)) {
        // uv_write2 descriptor passing completes asynchronously; remember net
        // streams by address so later cleanup never falls through to HTTP.
        net_pending_ipc_register_socket(sock);
        return (uv_stream_t*)&sock->tcp;
    }

    Item inner = js_property_get(handle_item, make_string_item("_handle"));
    if (net_is_object_like(inner) && inner.item != handle_item.item) {
        // sendHandle normally receives a net.Socket; unwrap its public object
        // to the native TCP handle used by uv_write2 descriptor passing.
        return js_net_stream_from_ipc_send_handle(inner);
    }
    uv_stream_t* http_stream = js_http_stream_from_ipc_send_handle(handle_item);
    if (http_stream) return http_stream;
    return NULL;
}

static JsServer* server_from_ipc_stream(uv_stream_t* stream) {
    // IPC cleanup can receive stream pointers after close callbacks run; only
    // rooted live wrappers may be dereferenced to identify a net handle.
    for (int i = 0; i < NET_ACTIVE_SERVER_MAX; i++) {
        JsServer* srv = server_from_object(net_active_servers[i]);
        if (srv && (uv_stream_t*)&srv->tcp == stream) return srv;
    }
    return NULL;
}

static JsSocket* socket_from_ipc_stream(uv_stream_t* stream) {
    // IPC cleanup can receive stream pointers after close callbacks run; only
    // rooted live wrappers may be dereferenced to identify a net handle.
    for (int i = 0; i < NET_ACTIVE_SOCKET_MAX; i++) {
        JsSocket* sock = socket_from_object(net_active_sockets[i]);
        if (sock && (uv_stream_t*)&sock->tcp == stream) return sock;
    }
    return NULL;
}

extern "C" void* js_net_ipc_sent_stream_connection_account(uv_stream_t* stream) {
    if (stream && uv_handle_get_type((uv_handle_t*)stream) == UV_NAMED_PIPE) return NULL;
    NetPendingIpcStream* pending = net_pending_ipc_find_stream(stream);
    JsSocket* sock = socket_from_ipc_stream(stream);
    if (!sock && pending) sock = pending->sock;
    if (!sock || !sock->owner_server) {
        if (pending) return NULL;
        void* http_account = js_http_ipc_sent_stream_connection_account(stream);
        // HTTP accepted sockets share IPC plumbing but use JsHttpServer
        // accounting; tag the aligned pointer so completion dispatches safely.
        return http_account ? (void*)((uintptr_t)http_account | (uintptr_t)1U) : NULL;
    }
    return (void*)sock->owner_server;
}

extern "C" void js_net_complete_transferred_connection_account(void* account) {
    uintptr_t raw = (uintptr_t)account;
    if ((raw & 1U) != 0) {
        js_http_complete_transferred_connection_account((void*)(raw & ~(uintptr_t)1U));
        return;
    }
    JsServer* srv = (JsServer*)account;
    if (!srv) return;
    if (srv->connection_count > 0) srv->connection_count--;
    server_maybe_finish_close(srv);
}

extern "C" void js_net_close_ipc_sent_stream(uv_stream_t* stream);

extern "C" void js_net_close_ipc_sent_stream_defer_account(uv_stream_t* stream) {
    NetPendingIpcStream* pending = net_pending_ipc_find_stream(stream);
    if (pending) pending->cleanup_seen = true;
    JsSocket* sock = socket_from_ipc_stream(stream);
    if (!sock && pending) sock = pending->sock;
    if (pending && pending->close_seen) {
        socket_free_detached(pending->sock);
        return;
    }
    if (sock && sock->owner_server) {
        sock->transfer_account_pending = true;
    } else if (!sock && js_http_close_ipc_sent_stream_defer_account(stream)) {
        return;
    }
    js_net_close_ipc_sent_stream(stream);
}

extern "C" void js_net_close_ipc_sent_stream(uv_stream_t* stream) {
    if (!stream) return;
    if (uv_handle_get_type((uv_handle_t*)stream) == UV_NAMED_PIPE) {
        JsPipeHandle* ph = (JsPipeHandle*)stream->data;
        if (!ph || ph->closed || uv_is_closing((uv_handle_t*)&ph->pipe)) return;
        ph->closed = true;
        uv_close((uv_handle_t*)&ph->pipe, pipe_handle_close_cb);
        return;
    }
    NetPendingIpcStream* pending = net_pending_ipc_find_stream(stream);
    if (pending) pending->cleanup_seen = true;
    if (pending && pending->close_seen) {
        socket_free_detached(pending->sock);
        return;
    }
    JsServer* srv = server_from_ipc_stream(stream);
    if (srv) {
        if (uv_is_closing((uv_handle_t*)&srv->tcp)) return;
        // IPC keepOpen=false transfers the sender's listening fd ownership;
        // leaving the server refed makes cluster tests wait for the drain watchdog.
        js_property_set(srv->js_object, make_string_item("__ipc_transferred_address__"),
                        js_server_address_for(srv->js_object));
        // keepOpen:false transfers native ownership immediately, but Node
        // surfaces sender-side 'close' when userland later calls server.close().
        srv->ipc_transfer_defer_close_event = true;
        server_close_handle_now(srv);
        return;
    }
    JsSocket* sock = socket_from_ipc_stream(stream);
    if (!sock && pending) sock = pending->sock;
    if (!sock || sock->destroyed) {
        if (!pending) js_http_close_ipc_sent_stream(stream);
        return;
    }
    if (uv_is_closing((uv_handle_t*)&sock->tcp)) return;
    socket_close_now(sock);
}

extern "C" void js_net_close_all_active_servers(void) {
    Item servers[NET_ACTIVE_SERVER_MAX];
    int count = 0;
    for (int i = 0; i < NET_ACTIVE_SERVER_MAX; i++) {
        if (net_active_servers[i].item) servers[count++] = net_active_servers[i];
    }
    for (int i = 0; i < count; i++) {
        JsServer* srv = server_from_object(servers[i]);
        if (!srv) continue;
        // cluster worker disconnect drains server handles before IPC closes;
        // otherwise worker-owned listeners keep the child alive until watchdog.
        server_close_handle_now(srv);
    }
}

static void server_connection_cb(uv_stream_t* server, int status) {
    if (status < 0) return;

    JsServer* srv = (JsServer*)server->data;
    if (!srv) return;

    uv_loop_t* loop = server->loop;

    JsSocket* client = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    client->high_water_mark = NET_SOCKET_DEFAULT_HIGH_WATER_MARK;
    uv_tcp_init(loop, &client->tcp);
    client->tcp.data = client;

    if (uv_accept(server, (uv_stream_t*)&client->tcp) == 0) {
        if (!context || !context->heap) {
            uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
                socket_free_detached(h ? (JsSocket*)h->data : NULL);
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
                    socket_free_detached(h ? (JsSocket*)h->data : NULL);
                });
                return;
            }
        }

        int max_connections = server_max_connections(srv);
        if (max_connections >= 0 && srv->connection_count >= max_connections) {
            server_emit_drop(srv, client);
            uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
                socket_free_detached(h ? (JsSocket*)h->data : NULL);
            });
            return;
        }

        Item client_handle = make_socket_handle_object(client);
        if (server_emit_cluster_worker_newconn(srv, client, client_handle)) return;
        Item server_handle = js_property_get(srv->js_object, make_string_item("_handle"));
        Item onconnection = js_property_get(server_handle, make_string_item("onconnection"));
        if (is_callable(onconnection)) {
            Item args[2] = { (Item){.item = i2it(0)}, client_handle };
            js_call_function(onconnection, server_handle, args, 2);
            js_microtask_flush();
        } else {
            server_accept_client(srv, client, client_handle);
        }
    } else {
        uv_close((uv_handle_t*)&client->tcp, [](uv_handle_t* h) {
            socket_free_detached(h ? (JsSocket*)h->data : NULL);
        });
    }
}

static void server_pipe_connection_cb(uv_stream_t* server, int status) {
    if (status < 0) return;
    uv_loop_t* loop = server ? server->loop : lambda_uv_loop();
    if (!loop) return;

    uv_pipe_t* client = (uv_pipe_t*)mem_calloc(1, sizeof(uv_pipe_t), MEM_CAT_JS_RUNTIME);
    if (!client) return;
    uv_pipe_init(loop, client, 0);
    if (uv_accept(server, (uv_stream_t*)client) != 0) {
        uv_close((uv_handle_t*)client, [](uv_handle_t* h) {
            if (h) mem_free(h);
        });
        return;
    }
    // Path servers currently need bind/listen/error semantics for cluster IPC
    // coordination; close accepted pipe clients until pipe sockets are modeled.
    uv_close((uv_handle_t*)client, [](uv_handle_t* h) {
        if (h) mem_free(h);
    });
}

static Item js_server_address_for(Item self) {
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL) {
        Item transferred = js_property_get(self, make_string_item("__ipc_transferred_address__"));
        return net_is_object_like(transferred) ? transferred : ItemNull;
    }
    JsServer* srv = (JsServer*)net_native_ptr_from_item(handle_item);
    if (!srv) {
        Item transferred = js_property_get(self, make_string_item("__ipc_transferred_address__"));
        return net_is_object_like(transferred) ? transferred : ItemNull;
    }

    struct sockaddr_storage addr;
    int addrlen = sizeof(addr);
    int r = uv_tcp_getsockname(&srv->tcp, (struct sockaddr*)&addr, &addrlen);
    if (r != 0) {
        // A keepOpen:false IPC server transfer closes sender ownership, but
        // userland still reads address().port to connect to the receiver.
        Item transferred = js_property_get(self, make_string_item("__ipc_transferred_address__"));
        return net_is_object_like(transferred) ? transferred : ItemNull;
    }

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
    } else {
        return ItemNull;
    }
    return result;
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

    JsServer* srv = (JsServer*)net_native_ptr_from_item(handle_item);
    if (!srv || srv->closed || !srv->listen_pending) return make_undefined_item();

    srv->listen_pending = false;
    server_emit(self, "listening", NULL, 0);
    js_cluster_notify_worker_listening(js_server_address_for(self));
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
    // listen callbacks must run after the current JS stack, not inside the
    // listen() native call; cluster setup assigns workers immediately after it.
    js_setTimeout(fn, (Item){.item = i2it(0)});
}

static NetClusterPipeServer* net_cluster_primary_find_pipe_server(const char* path, bool create) {
    if (!path || !path[0]) return NULL;
    for (int i = 0; i < NET_CLUSTER_PIPE_SERVER_MAX; i++) {
        if (net_cluster_pipe_servers[i].active &&
            strcmp(net_cluster_pipe_servers[i].path, path) == 0) {
            return &net_cluster_pipe_servers[i];
        }
    }
    if (!create) return NULL;

    uv_loop_t* loop = lambda_uv_loop();
    if (!loop) return NULL;
    for (int i = 0; i < NET_CLUSTER_PIPE_SERVER_MAX; i++) {
        NetClusterPipeServer* cps = &net_cluster_pipe_servers[i];
        if (cps->active || cps->closing) continue;
        memset(cps, 0, sizeof(NetClusterPipeServer));
        snprintf(cps->path, sizeof(cps->path), "%s", path);
        uv_pipe_init(loop, &cps->pipe, 0);
        cps->pipe.data = cps;
        char bind_path[4096];
        normalize_pipe_fs_path(cps->path, bind_path, (int)sizeof(bind_path));
        int r = uv_pipe_bind(&cps->pipe, bind_path);
        if (r != 0) {
            // uv_close keeps the embedded pipe on libuv's close queue; clearing
            // the slot before the callback corrupts that queue during drain.
            cps->closing = true;
            uv_close((uv_handle_t*)&cps->pipe, net_cluster_pipe_server_close_cb);
            return NULL;
        }
        r = uv_listen((uv_stream_t*)&cps->pipe, 128, [](uv_stream_t* stream, int status) {
            if (status < 0) return;
            NetClusterPipeServer* server = stream ? (NetClusterPipeServer*)stream->data : NULL;
            if (!server || server->closing) return;
            uv_loop_t* accept_loop = stream->loop;
            JsPipeHandle* ph = (JsPipeHandle*)mem_calloc(1, sizeof(JsPipeHandle), MEM_CAT_JS_RUNTIME);
            if (!ph) return;
            uv_pipe_init(accept_loop, &ph->pipe, 0);
            ph->pipe.data = ph;
            if (uv_accept(stream, (uv_stream_t*)&ph->pipe) != 0) {
                uv_close((uv_handle_t*)&ph->pipe, pipe_handle_close_cb);
                return;
            }

            Item handle = make_pipe_handle_object(ph);
            if (!net_cluster_primary_dispatch_pipe_connection(server, handle)) {
                Item close_fn = js_property_get(handle, make_string_item("close"));
                if (is_callable(close_fn)) {
                    js_call_function(close_fn, handle, NULL, 0);
                    js_microtask_flush();
                }
            }
        });
        if (r != 0) {
            // uv_close keeps the embedded pipe on libuv's close queue; clearing
            // the slot before the callback corrupts that queue during drain.
            cps->closing = true;
            uv_close((uv_handle_t*)&cps->pipe, net_cluster_pipe_server_close_cb);
            return NULL;
        }
        cps->active = true;
        return cps;
    }
    return NULL;
}

static NetClusterWorkerPipeServer* net_cluster_worker_find_pipe_server(const char* path, bool create) {
    if (!path || !path[0]) return NULL;
    for (int i = 0; i < NET_CLUSTER_PIPE_WORKER_MAX; i++) {
        if (net_cluster_worker_pipe_servers[i].active &&
            strcmp(net_cluster_worker_pipe_servers[i].path, path) == 0) {
            return &net_cluster_worker_pipe_servers[i];
        }
    }
    if (!create) return NULL;
    net_cluster_pipe_register_roots();
    for (int i = 0; i < NET_CLUSTER_PIPE_WORKER_MAX; i++) {
        NetClusterWorkerPipeServer* entry = &net_cluster_worker_pipe_servers[i];
        if (entry->active) continue;
        memset(entry, 0, sizeof(NetClusterWorkerPipeServer));
        snprintf(entry->path, sizeof(entry->path), "%s", path);
        entry->active = true;
        return entry;
    }
    return NULL;
}

static void net_cluster_worker_emit_pipe_listening(NetClusterWorkerPipeServer* entry) {
    if (!entry || !entry->active || entry->listening_emitted) return;
    JsServer* srv = server_from_object(entry->server_obj);
    if (!srv) return;
    entry->listening_emitted = true;
    srv->listen_pending = false;
    js_property_set(entry->server_obj, make_string_item("listening"), (Item){.item = ITEM_TRUE});
    server_emit(entry->server_obj, "listening", NULL, 0);
    js_cluster_notify_worker_listening(ItemNull);
    if (is_callable(entry->callback)) {
        js_call_function(entry->callback, entry->server_obj, NULL, 0);
        js_microtask_flush();
    }
}

static Item net_cluster_worker_send_pipe_query_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    NetClusterWorkerPipeServer* entry = env ? (NetClusterWorkerPipeServer*)(uintptr_t)env[0].item : NULL;
    if (!entry || !entry->active || entry->query_sent) return make_undefined_item();
    JsServer* srv = server_from_object(entry->server_obj);
    if (!srv) return make_undefined_item();
    entry->query_sent = true;

    Item process_obj = js_get_process_object_value();
    Item send = js_property_get(process_obj, make_string_item("send"));
    if (!is_callable(send)) return make_undefined_item();
    Item message = js_new_object();
    js_property_set(message, make_string_item("__lambda_cluster_query_server__"),
                    (Item){.item = ITEM_TRUE});
    js_property_set(message, make_string_item("addressType"), make_string_item("pipe"));
    js_property_set(message, make_string_item("path"), make_string_item(entry->path));
    js_property_set(message, make_string_item("key"), make_string_item(entry->path));
    js_property_set(message, make_string_item("maxConnections"),
                    (Item){.item = i2it(server_max_connections(srv))});
    // Cluster workers must ask the primary to own SCHED_RR pipe listeners;
    // direct worker binds make only one worker observable and leave the rest idle.
    Item sent = js_call_function(send, process_obj, &message, 1);
    js_microtask_flush();
    if (sent.item == ITEM_TRUE || sent.item == b2it(true)) {
        // once the query is queued, the worker's listen callback may run; waiting
        // for a second IPC ack can leave sibling workers stuck before user setup.
        net_cluster_worker_emit_pipe_listening(entry);
    }
    return make_undefined_item();
}

static bool net_cluster_worker_register_pipe_server(Item self, JsServer* srv,
                                                    const char* path, Item callback) {
    if (!srv || !path || !path[0] || !js_cluster_is_worker_runtime() ||
        !js_cluster_worker_uses_sched_rr()) {
        return false;
    }
    NetClusterWorkerPipeServer* entry = net_cluster_worker_find_pipe_server(path, true);
    if (!entry) return false;
    int index = (int)(entry - net_cluster_worker_pipe_servers);
    entry->server_obj = self;
    entry->callback = callback;
    net_cluster_worker_server_roots[index] = self;
    net_cluster_worker_callback_roots[index] = callback;
    srv->is_pipe = true;
    srv->listen_pending = true;
    js_property_set(self, make_string_item("__lambda_cluster_pipe_path__"), make_string_item(path));
    Item* env = js_alloc_env(1);
    env[0] = (Item){.item = (uint64_t)(uintptr_t)entry};
    // maxConnections is often assigned immediately after listen(); scheduling
    // after the current script turn preserves that async listen configuration edge.
    js_setTimeout(js_new_closure((void*)net_cluster_worker_send_pipe_query_tick, 0, env, 1),
                  (Item){.item = i2it(0)});
    return true;
}

static bool net_cluster_primary_handle_pipe_ack(Item worker, Item message) {
    Item is_ack = js_property_get(message, make_string_item("__lambda_cluster_pipe_ack__"));
    if (is_ack.item != ITEM_TRUE && is_ack.item != b2it(true)) return false;
    String* path = it2s(js_property_get(message, make_string_item("path")));
    if (!path || path->len <= 0) return true;
    char path_buf[4096];
    int len = (int)path->len < (int)sizeof(path_buf) - 1 ? (int)path->len : (int)sizeof(path_buf) - 1;
    memcpy(path_buf, path->chars, (size_t)len);
    path_buf[len] = '\0';

    NetClusterPipeServer* server = net_cluster_primary_find_pipe_server(path_buf, false);
    if (!server) return true;
    int64_t seq = net_cluster_int64_value(js_property_get(message, make_string_item("seq")), -1);
    NetClusterPipePending* pending = net_cluster_primary_find_pending(server, seq);
    if (!pending) return true;
    bool accepted = net_cluster_bool_value(js_property_get(message, make_string_item("accepted")));
    if (accepted) {
        net_cluster_close_pipe_handle_item(pending->handle);
        net_cluster_primary_clear_pending(server, pending);
        return true;
    }

    if (pending->selected_worker >= 0 &&
        pending->selected_worker < NET_CLUSTER_PIPE_WORKER_MAX) {
        pending->rejected_workers[pending->selected_worker] = true;
    }
    for (int i = 0; i < NET_CLUSTER_PIPE_WORKER_MAX; i++) {
        if (server->workers[i].active && server->workers[i].worker.item == worker.item) {
            pending->rejected_workers[i] = true;
            break;
        }
    }
    pending->selected_worker = -1;
    pending->in_flight = false;
    if (net_cluster_primary_send_pending(server, pending)) return true;
    net_cluster_close_pipe_handle_item(pending->handle);
    net_cluster_primary_clear_pending(server, pending);
    return true;
}

extern "C" bool js_net_cluster_primary_handle_message(Item worker, Item message) {
    if (!net_is_object_like(message) || !net_is_object_like(worker)) return false;
    if (net_cluster_primary_handle_pipe_ack(worker, message)) return true;
    Item is_query = js_property_get(message, make_string_item("__lambda_cluster_query_server__"));
    if (is_query.item != ITEM_TRUE && is_query.item != b2it(true)) return false;
    if (!js_is_truthy(js_property_get(message, make_string_item("path")))) return true;
    String* path = it2s(js_property_get(message, make_string_item("path")));
    if (!path || path->len <= 0) return true;
    char path_buf[4096];
    int len = (int)path->len < (int)sizeof(path_buf) - 1 ? (int)path->len : (int)sizeof(path_buf) - 1;
    memcpy(path_buf, path->chars, (size_t)len);
    path_buf[len] = '\0';

    NetClusterPipeServer* server = net_cluster_primary_find_pipe_server(path_buf, true);
    if (!server) return true;
    net_cluster_pipe_register_roots();
    for (int i = 0; i < NET_CLUSTER_PIPE_WORKER_MAX; i++) {
        NetClusterPipeWorker* entry = &server->workers[i];
        if (entry->active && entry->worker.item == worker.item) return true;
    }
    for (int i = 0; i < NET_CLUSTER_PIPE_WORKER_MAX; i++) {
        NetClusterPipeWorker* entry = &server->workers[i];
        if (entry->active) continue;
        entry->active = true;
        entry->worker = worker;
        Item max_item = js_property_get(message, make_string_item("maxConnections"));
        entry->max_connections = get_type_id(max_item) == LMD_TYPE_INT ? (int)it2i(max_item) : -1;
        int server_index = (int)(server - net_cluster_pipe_servers);
        net_cluster_pipe_worker_roots[server_index][i] = worker;
        break;
    }

    Item reply = js_new_object();
    js_property_set(reply, make_string_item("__lambda_cluster_server_listening__"),
                    (Item){.item = ITEM_TRUE});
    js_property_set(reply, make_string_item("path"), make_string_item(path_buf));
    Item send = js_property_get(worker, make_string_item("send"));
    if (is_callable(send)) {
        Item sent_reply = js_call_function(send, worker, &reply, 1);
        (void)sent_reply;
        js_microtask_flush();
    }
    return true;
}

static void net_cluster_worker_send_pipe_ack(const char* path, int64_t seq, bool accepted) {
    if (!path || !path[0]) return;
    Item process_obj = js_get_process_object_value();
    Item send = js_property_get(process_obj, make_string_item("send"));
    if (!is_callable(send)) return;
    Item message = js_new_object();
    js_property_set(message, make_string_item("__lambda_cluster_pipe_ack__"),
                    (Item){.item = ITEM_TRUE});
    js_property_set(message, make_string_item("path"), make_string_item(path));
    js_property_set(message, make_string_item("seq"), (Item){.item = i2it(seq)});
    js_property_set(message, make_string_item("accepted"), (Item){.item = b2it(accepted)});
    js_call_function(send, process_obj, &message, 1);
    js_microtask_flush();
}

extern "C" bool js_net_cluster_worker_handle_primary_message(Item message, Item handle) {
    if (!net_is_object_like(message)) return false;
    Item is_listening = js_property_get(message, make_string_item("__lambda_cluster_server_listening__"));
    Item is_newconn = js_property_get(message, make_string_item("__lambda_cluster_pipe_newconn__"));
    if (is_listening.item != ITEM_TRUE && is_listening.item != b2it(true) &&
        is_newconn.item != ITEM_TRUE && is_newconn.item != b2it(true)) {
        return false;
    }
    String* path = it2s(js_property_get(message, make_string_item("path")));
    if (!path || path->len <= 0) return true;
    char path_buf[4096];
    int len = (int)path->len < (int)sizeof(path_buf) - 1 ? (int)path->len : (int)sizeof(path_buf) - 1;
    memcpy(path_buf, path->chars, (size_t)len);
    path_buf[len] = '\0';

    NetClusterWorkerPipeServer* entry = net_cluster_worker_find_pipe_server(path_buf, false);
    if (!entry || !entry->active) return true;
    JsServer* srv = server_from_object(entry->server_obj);
    if (!srv) return true;

    if (is_listening.item == ITEM_TRUE || is_listening.item == b2it(true)) {
        net_cluster_worker_emit_pipe_listening(entry);
        return true;
    }

    int64_t seq = net_cluster_int64_value(js_property_get(message, make_string_item("seq")), -1);
    int max_connections = server_max_connections(srv);
    if (!net_is_object_like(handle) ||
        (max_connections >= 0 && srv->connection_count >= max_connections)) {
        net_cluster_worker_send_pipe_ack(path_buf, seq, false);
        net_cluster_close_pipe_handle_item(handle);
        return true;
    }
    srv->connection_count++;
    // the primary owns RR accept and waits for worker capacity feedback; emit
    // only after accepting so rejected handles can be rerouted while still open.
    net_cluster_worker_send_pipe_ack(path_buf, seq, true);
    Item socket_obj = make_cluster_pipe_socket_object(handle, srv);
    if (get_type_id(srv->connection_handler) == LMD_TYPE_FUNC) {
        js_call_function(srv->connection_handler, entry->server_obj, &socket_obj, 1);
        js_microtask_flush();
    }
    server_emit(entry->server_obj, "connection", &socket_obj, 1);
    return true;
}

static Item js_server_emit_error_scheduled(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    Item err = env[1];
    server_emit(self, "error", &err, 1);
    if (js_check_exception()) {
        Item thrown = js_clear_exception();
        // server 'error' listeners run asynchronously; exceptions thrown there
        // must route through process.uncaughtException before fatal shutdown.
        Item handled = js_process_emit(make_string_item("uncaughtException"), thrown);
        if (js_check_exception()) return make_undefined_item();
        if (handled.item != ITEM_TRUE && handled.item != b2it(true)) {
            js_throw_value(thrown);
        }
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

static void server_ref_listening_handle(JsServer* srv) {
    uv_handle_t* handle = server_handle(srv);
    if (!handle || uv_is_closing(handle)) return;
    // listen() is a liveness root by default; forked children can otherwise
    // exit before the deferred listening callback starts their server flow.
    uv_ref(handle);
}

static void server_close_if_worker_disconnect_requested(JsServer* srv) {
    if (!srv || !js_process_ipc_worker_disconnect_requested()) return;
    // cluster disconnect can reach a worker before user code calls listen();
    // close late listeners because the earlier active-server drain missed them.
    server_close_handle_now(srv);
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

static bool server_signal_is_aborted(Item signal) {
    if (!net_is_object_like(signal)) return false;
    Item aborted = js_property_get(signal, make_string_item("aborted"));
    return get_type_id(aborted) == LMD_TYPE_BOOL && it2b(aborted);
}

static Item js_server_abort_signal_event(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    Item close_fn = js_property_get(self, make_string_item("close"));
    if (is_callable(close_fn)) {
        js_call_function(close_fn, self, NULL, 0);
        js_microtask_flush();
    }
    return make_undefined_item();
}

static bool server_configure_listen_signal(Item self, Item signal, bool* out_aborted) {
    if (out_aborted) *out_aborted = false;
    if (is_undefined_item(signal) || signal.item == ITEM_NULL) return true;
    if (!net_is_object_like(signal)) {
        js_throw_invalid_arg_type("options.signal", "AbortSignal", signal);
        return false;
    }
    if (out_aborted) *out_aborted = server_signal_is_aborted(signal);
    Item add_fn = js_property_get(signal, make_string_item("addEventListener"));
    if (is_callable(add_fn)) {
        Item* env = js_alloc_env(1);
        env[0] = self;
        Item handler = js_new_closure((void*)js_server_abort_signal_event, 0, env, 1);
        Item args[2] = { make_string_item("abort"), handler };
        js_call_function(add_fn, signal, args, 2);
        js_microtask_flush();
    }
    return true;
}

// server.listen(port, [host], [callback])
extern "C" Item js_server_listen(Item port_item, Item host_item, Item callback) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0 || handle_item.item == ITEM_NULL || is_undefined_item(handle_item)) {
        return self;
    }
    // Worker-mode native pointers may be boxed as INT64; rejecting them makes
    // cluster workers return from listen() without binding.
    JsServer* srv = (JsServer*)net_native_ptr_from_item(handle_item);
    if (!srv) {
        return self;
    }

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

    if (!net_permission_allowed()) {
        // listen denial is synchronous in Node, and checking before bind/listen
        // avoids leaving socket files or referenced server handles behind.
        Item err = js_permission_make_net_error("listen", NULL);
        js_throw_value(err);
        return ItemNull;
    }

    if (srv->closed && srv->close_requested && !srv->handle_closed) {
        srv->listen_after_close = true;
        srv->pending_listen_port = port_item;
        srv->pending_listen_host = host_item;
        srv->pending_listen_callback = callback;
        return self;
    }

    if (!srv->closed && !srv->close_requested &&
        (srv->listen_pending || uv_is_active(server_handle(srv)))) {
        return js_throw_error_with_code(
            "ERR_SERVER_ALREADY_LISTEN",
            "Listen method has been called more than once without closing.");
    }

    if (srv->closed && srv->handle_closed) {
        uv_loop_t* loop = lambda_uv_loop();
        if (!loop) return self;
        uv_tcp_init(loop, &srv->tcp);
        srv->tcp.data = srv;
        srv->closed = false;
        srv->listen_pending = false;
        srv->close_requested = false;
        srv->handle_closed = false;
        srv->close_event_emitted = false;
        srv->ipc_transfer_defer_close_event = false;
        srv->connection_count = 0;
    }

    int port = 0;
    char host_buf[256] = "0.0.0.0";
    char pipe_path[4096];
    pipe_path[0] = '\0';
    bool use_pipe_path = false;
    bool pipe_readable_all = false;
    bool pipe_writable_all = false;
    bool ipv6_only = false;
    bool reuse_port = false;
    bool listen_signal_aborted = false;

    JsBoundSocket* bound_listen = bound_socket_from_item(port_item);
    if (bound_listen) {
        if (bound_listen->closed || bound_listen->adopted) {
            return bound_socket_throw_adopted();
        }
        int fd = bound_socket_dup_fd(bound_listen);
        if (fd < 0) {
            Item err = make_uv_error(UV_EBADF, "listen", NULL, -1);
            server_schedule_error(self, err);
            return self;
        }
        int open_r = uv_tcp_open(&srv->tcp, (uv_os_sock_t)fd);
        if (open_r != 0) {
#ifndef _WIN32
            close(fd);
#endif
            Item err = make_uv_error(open_r, "listen", NULL, -1);
            server_schedule_error(self, err);
            server_close_after_listen_error(srv);
            return self;
        }
        int listen_r = uv_listen((uv_stream_t*)&srv->tcp, 128, server_connection_cb);
        if (listen_r != 0) {
            Item err = make_uv_error(listen_r, "listen", NULL, -1);
            server_schedule_error(self, err);
            server_close_after_listen_error(srv);
            return self;
        }
        server_ref_listening_handle(srv);
        net_active_add(net_active_servers, NET_ACTIVE_SERVER_MAX, self);
        server_update_connection_key(self, srv, 0);
        js_property_set(self, make_string_item("listening"), (Item){.item = ITEM_TRUE});
        server_schedule_listening(self, srv, callback);
        server_close_if_worker_disconnect_requested(srv);
        return self;
    }

    TypeId port_type = get_type_id(port_item);
    if (port_type == LMD_TYPE_MAP || port_type == LMD_TYPE_OBJECT || port_type == LMD_TYPE_VMAP) {
        bool has_port = net_object_has_key(port_item, "port");
        bool has_path = net_object_has_key(port_item, "path");
        bool has_fd = net_object_has_key(port_item, "fd");
        if (has_fd) {
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
            if (!valid_fd_number) {
                js_throw_invalid_arg_type("options.fd", "number", fd_item);
                return self;
            }
            if (fd_value < 0) {
                js_throw_type_error_code(
                    "ERR_INVALID_ARG_VALUE",
                    "The argument 'options' must have the property \"port\" or \"path\". Received an instance of Object");
                return self;
            }
            int open_r = uv_tcp_open(&srv->tcp, (uv_os_sock_t)fd_value);
            if (open_r != 0) {
                Item err = make_uv_error(open_r, "listen", NULL, -1);
                server_schedule_error(self, err);
                server_close_after_listen_error(srv);
                return self;
            }
            int listen_r = uv_listen((uv_stream_t*)&srv->tcp, 128, server_connection_cb);
            if (listen_r != 0) {
                Item err = make_uv_error(listen_r, "listen", NULL, -1);
                server_schedule_error(self, err);
                server_close_after_listen_error(srv);
                return self;
            }
            server_ref_listening_handle(srv);
            net_active_add(net_active_servers, NET_ACTIVE_SERVER_MAX, self);
            server_update_connection_key(self, srv, 0);
            js_property_set(self, make_string_item("listening"), (Item){.item = ITEM_TRUE});
            server_schedule_listening(self, srv, callback);
            server_close_if_worker_disconnect_requested(srv);
            return self;
        }
        if (!has_port && !has_path) {
            js_throw_type_error_code(
                "ERR_INVALID_ARG_VALUE",
                "The argument 'options' must have the property \"port\" or \"path\". Received an instance of Object");
            return self;
        }
        Item opt_port = js_property_get(port_item, make_string_item("port"));
        if (has_port) {
            if (is_undefined_item(opt_port) || opt_port.item == ITEM_NULL) {
                port = 0;
            } else if (get_type_id(opt_port) == LMD_TYPE_BOOL) {
                js_throw_type_error_code(
                    "ERR_INVALID_ARG_VALUE",
                    "The argument 'options' is invalid. Received an instance of Object");
                return self;
            } else if (!parse_port(opt_port, &port)) {
                return self;
            }
        } else {
            Item opt_path = js_property_get(port_item, make_string_item("path"));
            if (get_type_id(opt_path) != LMD_TYPE_STRING) {
                js_throw_type_error_code(
                    "ERR_INVALID_ARG_VALUE",
                    "The argument 'options' is invalid. Received an instance of Object");
                return self;
            }
            String* path = it2s(opt_path);
            int len = (int)path->len < (int)sizeof(pipe_path) - 1 ?
                (int)path->len : (int)sizeof(pipe_path) - 1;
            memcpy(pipe_path, path->chars, (size_t)len);
            pipe_path[len] = '\0';
            use_pipe_path = true;
            pipe_readable_all = js_is_truthy(js_property_get(port_item, make_string_item("readableAll")));
            pipe_writable_all = js_is_truthy(js_property_get(port_item, make_string_item("writableAll")));
        }
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
        Item opt_signal = js_property_get(port_item, make_string_item("signal"));
        if (!server_configure_listen_signal(self, opt_signal, &listen_signal_aborted)) return self;
    } else if (port_type == LMD_TYPE_STRING) {
        String* s = it2s(port_item);
        char first[256];
        int len = (int)s->len < 255 ? (int)s->len : 255;
        memcpy(first, s->chars, (size_t)len);
        first[len] = '\0';

        char* end = NULL;
        long parsed = strtol(first, &end, 0);
        if (end == first || *end != '\0' || parsed < 0 || parsed > 65535) {
            snprintf(pipe_path, sizeof(pipe_path), "%s", first);
            use_pipe_path = true;
        } else {
            port = (int)parsed;
        }
    } else {
        if (port_type == LMD_TYPE_BOOL) {
            js_throw_type_error_code(
                "ERR_INVALID_ARG_VALUE",
                "The argument 'options' is invalid. Received false");
            return self;
        }
        if (!parse_port(port_item, &port)) return self;
    }

    if (get_type_id(host_item) == LMD_TYPE_STRING) {
        String* h = it2s(host_item);
        int len = (int)h->len < 255 ? (int)h->len : 255;
        memcpy(host_buf, h->chars, (size_t)len);
        host_buf[len] = '\0';
    }

    if (use_pipe_path) {
        // object-form permission listens must bind locally so readableAll /
        // writableAll chmod applies to the socket file being asserted.
        if (!pipe_readable_all && !pipe_writable_all &&
            net_cluster_worker_register_pipe_server(self, srv, pipe_path, callback)) {
            return self;
        }
        uv_loop_t* loop = lambda_uv_loop();
        if (!loop) return self;
        srv->is_pipe = true;
        uv_pipe_init(loop, &srv->pipe, 0);
        srv->pipe.data = srv;

        char pipe_fs_path[4096];
        normalize_pipe_fs_path(pipe_path, pipe_fs_path, (int)sizeof(pipe_fs_path));
        int pipe_r = uv_pipe_bind(&srv->pipe, pipe_fs_path);
        if (pipe_r != 0) {
            Item err = make_uv_error(pipe_r, "listen", pipe_path, -1);
            // string listen() is an IPC path, not a numeric port; surfacing
            // bind failures lets nested cluster workers report EADDRINUSE.
            server_schedule_error(self, err);
            server_close_after_listen_error(srv);
            return self;
        }
#ifndef _WIN32
        if (pipe_readable_all || pipe_writable_all) {
            mode_t mode = 0700;
            if (pipe_readable_all) mode |= 0555;
            if (pipe_writable_all) mode |= 0333;
            // object-form pipe listen owns the socket-file mode; missing chmod
            // makes official tests throw before they can close/disconnect.
            chmod(pipe_fs_path, mode);
        }
#endif

        pipe_r = uv_listen((uv_stream_t*)&srv->pipe, 128, server_pipe_connection_cb);
        if (pipe_r != 0) {
            Item err = make_uv_error(pipe_r, "listen", pipe_path, -1);
            server_schedule_error(self, err);
            server_close_after_listen_error(srv);
            return self;
        }

        server_ref_listening_handle(srv);
        net_active_add(net_active_servers, NET_ACTIVE_SERVER_MAX, self);
        js_property_set(self, make_string_item("listening"), (Item){.item = ITEM_TRUE});
        server_schedule_listening(self, srv, callback);
        server_close_if_worker_disconnect_requested(srv);
        return self;
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
        server_close_after_listen_error(srv);
        return self;
    }
    server_update_connection_key(self, srv, port);

    r = uv_listen((uv_stream_t*)&srv->tcp, 128, server_connection_cb);
    if (r != 0) {
        srv->listen_pending = false;
        Item err = make_uv_error(r, "listen", host_buf, port);
        server_schedule_error(self, err);
        server_close_after_listen_error(srv);
        return self;
    }

    server_ref_listening_handle(srv);
    net_active_add(net_active_servers, NET_ACTIVE_SERVER_MAX, self);
    js_property_set(self, make_string_item("listening"), (Item){.item = ITEM_TRUE});
    server_schedule_listening(self, srv, callback);
    server_close_if_worker_disconnect_requested(srv);
    if (listen_signal_aborted) {
        Item close_fn = js_property_get(self, make_string_item("close"));
        if (is_callable(close_fn)) {
            js_call_function(close_fn, self, NULL, 0);
            js_microtask_flush();
        }
    }
    return self;
}

// server.address() — returns {address, family, port} of the listening socket
static Item js_server_address(void) {
    Item self = js_get_this();
    return js_server_address_for(self);
}

static JsServer* server_from_object(Item self) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_OBJECT && type != LMD_TYPE_VMAP) return NULL;
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    return (JsServer*)net_native_ptr_from_item(handle_item);
}

// server.ref() / server.unref()
static Item js_server_ref(void) {
    Item self = js_get_this();
    JsServer* srv = server_from_object(self);
    uv_handle_t* handle = server_handle(srv);
    if (handle && !uv_is_closing(handle)) {
        uv_ref(handle);
    }
    return self;
}

static Item js_server_unref(void) {
    Item self = js_get_this();
    JsServer* srv = server_from_object(self);
    uv_handle_t* handle = server_handle(srv);
    if (handle && !uv_is_closing(handle)) {
        uv_unref(handle);
    }
    return self;
}

static Item js_server_getConnections_later(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    Item callback = env[1];
    Item count = env[2];
    if (is_callable(callback)) {
        Item args[2] = { ItemNull, count };
        js_call_function(callback, self, args, 2);
        js_microtask_flush();
    }
    return make_undefined_item();
}

// server.getConnections(callback)
static Item js_server_getConnections(Item callback) {
    Item self = js_get_this();
    JsServer* srv = server_from_object(self);
    int connections = srv ? srv->connection_count : 0;
    if (is_callable(callback)) {
        Item* env = js_alloc_env(3);
        env[0] = self;
        env[1] = callback;
        env[2] = (Item){.item = i2it(connections)};
        // getConnections is asynchronous in Node; calling the callback inside
        // an IPC message listener reenters teardown before transfer accounting settles.
        js_next_tick_enqueue(js_new_closure((void*)js_server_getConnections_later, 0, env, 3));
    }
    return self;
}

// server.close()
extern "C" Item js_server_close(Item callback) {
    Item self = js_get_this();
    Item handle_item = js_property_get(self, make_string_item("__server__"));
    if (handle_item.item == 0) return self;
    JsServer* srv = (JsServer*)net_native_ptr_from_item(handle_item);
    if (!srv) return self;
    srv->closed = true;
    srv->close_requested = true;
    srv->listen_pending = false;
    if (is_callable(callback)) {
        js_property_set(self, make_string_item("__close_callback__"), callback);
    }

    srv->ipc_transfer_defer_close_event = false;
    server_close_handle_now(srv);
    return self;
}

// server.on(event, callback)
extern "C" Item js_server_on(Item event_item, Item callback) {
    Item self = js_get_this();
    server_add_listener(self, event_item, callback, false);
    return self;
}

extern "C" Item js_server_once(Item event_item, Item callback) {
    Item self = js_get_this();
    server_add_listener(self, event_item, callback, true);
    return self;
}

extern "C" Item js_server_listeners(Item event_item) {
    Item self = js_get_this();
    Item result = js_array_new(0);
    if (get_type_id(event_item) != LMD_TYPE_STRING) return result;
    Item listeners = server_listener_map(self, false);
    if (get_type_id(listeners) != LMD_TYPE_MAP && get_type_id(listeners) != LMD_TYPE_OBJECT &&
        get_type_id(listeners) != LMD_TYPE_VMAP) {
        return result;
    }
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return result;
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item callback = server_listener_fn(js_array_get_int(arr, i));
        if (is_callable(callback)) js_array_push(result, callback);
    }
    return result;
}

extern "C" Item js_server_removeListener(Item event_item, Item callback) {
    Item self = js_get_this();
    if (get_type_id(event_item) != LMD_TYPE_STRING) return self;
    Item listeners = server_listener_map(self, false);
    if (get_type_id(listeners) != LMD_TYPE_MAP && get_type_id(listeners) != LMD_TYPE_OBJECT &&
        get_type_id(listeners) != LMD_TYPE_VMAP) {
        return self;
    }
    Item arr = js_property_get(listeners, event_item);
    if (get_type_id(arr) != LMD_TYPE_ARRAY) return self;

    Item next = js_array_new(0);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item record = js_array_get_int(arr, i);
        Item listener = server_listener_fn(record);
        if (listener.item != callback.item) js_array_push(next, record);
    }
    js_property_set(listeners, event_item, next);
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

    if (!is_undefined_item(options) && options.item != ITEM_NULL && !net_is_object_like(options)) {
        js_throw_invalid_arg_type("options", "Object", options);
        return ItemNull;
    }

    JsServer* srv = (JsServer*)mem_calloc(1, sizeof(JsServer), MEM_CAT_JS_RUNTIME);
    uv_tcp_init(loop, &srv->tcp);
    srv->tcp.data = srv;
    srv->connection_handler = handler;
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT ||
        get_type_id(options) == LMD_TYPE_VMAP) {
        Item allow_half_open = js_property_get(options, make_string_item("allowHalfOpen"));
        srv->allow_half_open = get_type_id(allow_half_open) == LMD_TYPE_BOOL && it2b(allow_half_open);
        Item keep_alive = js_property_get(options, make_string_item("keepAlive"));
        srv->keep_alive = get_type_id(keep_alive) == LMD_TYPE_BOOL && it2b(keep_alive);
        Item keep_alive_delay = js_property_get(options, make_string_item("keepAliveInitialDelay"));
        if (!is_undefined_item(keep_alive_delay) && keep_alive_delay.item != ITEM_NULL) {
            Item num = js_to_number(keep_alive_delay);
            double d = net_number_value(num);
            srv->keep_alive_initial_delay_ms = d > 0 ? (int)d : 0;
        }
        Item no_delay = js_property_get(options, make_string_item("noDelay"));
        srv->no_delay = get_type_id(no_delay) == LMD_TYPE_BOOL && it2b(no_delay);
        Item pause_on_connect = js_property_get(options, make_string_item("pauseOnConnect"));
        srv->pause_on_connect = get_type_id(pause_on_connect) == LMD_TYPE_BOOL && it2b(pause_on_connect);
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
    js_property_set(obj, make_string_item("_handle"), make_server_handle_object(srv));
    js_property_set(obj, make_string_item("listen"),
                    js_new_function((void*)js_server_listen, 3));
    js_property_set(obj, make_string_item("close"),
                    js_new_function((void*)js_server_close, 1));
    js_property_set(obj, make_string_item("on"),
                    js_new_function((void*)js_server_on, 2));
    js_property_set(obj, make_string_item("once"),
                    js_new_function((void*)js_server_once, 2));
    js_property_set(obj, make_string_item("listeners"),
                    js_new_function((void*)js_server_listeners, 1));
    js_property_set(obj, make_string_item("removeListener"),
                    js_new_function((void*)js_server_removeListener, 2));
    js_property_set(obj, make_string_item("off"),
                    js_new_function((void*)js_server_removeListener, 2));
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
    js_property_set(obj, make_string_item("keepAlive"),
                    (Item){.item = b2it(srv->keep_alive)});
    js_property_set(obj, make_string_item("keepAliveInitialDelay"),
                    (Item){.item = i2it(srv->keep_alive_initial_delay_ms)});
    js_property_set(obj, make_string_item("noDelay"),
                    (Item){.item = b2it(srv->no_delay)});
    js_property_set(obj, make_string_item("pauseOnConnect"),
                    (Item){.item = b2it(srv->pause_on_connect)});
    js_property_set(obj, make_string_item("listening"), (Item){.item = ITEM_FALSE});
    if (srv->has_block_list) {
        js_property_set(obj, make_string_item("__block_list__"), srv->block_list);
    }

    srv->js_object = obj;
    net_active_add(net_active_servers, NET_ACTIVE_SERVER_MAX, obj);
    return obj;
}

static Item make_server_object_from_fd(uv_loop_t* loop, int fd) {
    (void)loop;
    Item args = js_array_new(0);
    Item obj = js_net_createServer(args);
    JsServer* srv = server_from_object(obj);
    if (!srv) {
#ifndef _WIN32
        close(fd);
#endif
        return make_undefined_item();
    }
    int open_r = uv_tcp_open(&srv->tcp, (uv_os_sock_t)fd);
    if (open_r != 0) {
#ifndef _WIN32
        close(fd);
#endif
        return make_undefined_item();
    }
    int listen_r = uv_listen((uv_stream_t*)&srv->tcp, 128, server_connection_cb);
    if (listen_r != 0) {
        server_close_handle_now(srv);
        return make_undefined_item();
    }
    js_property_set(obj, make_string_item("listening"), (Item){.item = ITEM_TRUE});
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

    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT ||
        get_type_id(options) == LMD_TYPE_VMAP) {
        Item fd = js_property_get(options, make_string_item("fd"));
        if (!is_undefined_item(fd) && fd.item != ITEM_NULL) {
            TypeId fd_type = get_type_id(fd);
            bool valid_fd_number = false;
            int64_t fd_value = 0;
            if (fd_type == LMD_TYPE_INT) {
                fd_value = it2i(fd);
                valid_fd_number = true;
            } else if (fd_type == LMD_TYPE_INT64) {
                fd_value = it2l(fd);
                valid_fd_number = true;
            }
            if (!valid_fd_number) {
                js_throw_invalid_arg_type("options.fd", "number", fd);
                return ItemNull;
            }
            if (fd_value < 0) {
                js_throw_out_of_range("options.fd", ">= 0", fd);
                return ItemNull;
            }
        }
    }

    JsSocket* sock = (JsSocket*)mem_calloc(1, sizeof(JsSocket), MEM_CAT_JS_RUNTIME);
    sock->high_water_mark = NET_SOCKET_DEFAULT_HIGH_WATER_MARK;
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT ||
        get_type_id(options) == LMD_TYPE_VMAP) {
        Item hwm = js_property_get(options, make_string_item("highWaterMark"));
        if (get_type_id(hwm) == LMD_TYPE_INT && it2i(hwm) >= 0) {
            sock->high_water_mark = it2i(hwm);
        }
    }
    uv_tcp_init(loop, &sock->tcp);
    sock->tcp.data = sock;

    Item bound_handle = make_undefined_item();
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT ||
        get_type_id(options) == LMD_TYPE_VMAP) {
        bound_handle = js_property_get(options, make_string_item("handle"));
        JsBoundSocket* bound = bound_socket_from_item(bound_handle);
        if (bound) {
            int fd = bound_socket_dup_fd(bound);
            if (fd < 0 || uv_tcp_open(&sock->tcp, (uv_os_sock_t)fd) != 0) {
#ifndef _WIN32
                if (fd >= 0) close(fd);
#endif
                socket_free_detached(sock);
                js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                         "The property 'options.handle' is invalid");
                return ItemNull;
            }
            sock->adopted_bound_socket = true;
        }
    }

    uv_unref((uv_handle_t*)&sock->tcp);
    Item obj = make_socket_object(sock, false);
    if (get_type_id(options) == LMD_TYPE_MAP || get_type_id(options) == LMD_TYPE_OBJECT ||
        get_type_id(options) == LMD_TYPE_VMAP) {
        Item handle = js_property_get(options, make_string_item("handle"));
        if (sock->adopted_bound_socket) {
            js_property_set(obj, make_string_item("_handle"), make_socket_handle_object(sock));
            sock->handle_exposed = true;
        } else if (get_type_id(handle) == LMD_TYPE_MAP || get_type_id(handle) == LMD_TYPE_OBJECT ||
            get_type_id(handle) == LMD_TYPE_VMAP) {
            js_property_set(obj, make_string_item("_handle"), handle);
            Item native_handle = js_property_get(handle, make_string_item("__socket_handle__"));
            if (get_type_id(native_handle) != LMD_TYPE_INT) {
                js_property_set(handle, make_string_item("__socket_object__"), obj);
                js_property_set(handle, make_string_item("onread"),
                                js_new_function((void*)js_socket_js_handle_onread, 0));
            }
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
        Item allow_half_open = js_property_get(options, make_string_item("allowHalfOpen"));
        if (get_type_id(allow_half_open) == LMD_TYPE_BOOL) {
            js_property_set(obj, make_string_item("allowHalfOpen"), allow_half_open);
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
    socket_sync_no_half_open_listener(obj);
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

static Item js_stream_wrap_emit_error(Item env_item, Item err) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    socket_emit(env[0], "error", &err, 1);
    return make_undefined_item();
}

static Item js_stream_wrap_emit_end(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item wrap = env[0];
    if (js_property_get(wrap, make_string_item("__stream_wrap_ended__")).item == ITEM_TRUE) {
        return make_undefined_item();
    }
    js_property_set(wrap, make_string_item("__stream_wrap_ended__"), (Item){.item = ITEM_TRUE});
    js_property_set(wrap, make_string_item("readable"), (Item){.item = ITEM_FALSE});
    socket_emit(wrap, "end", NULL, 0);
    return make_undefined_item();
}

static Item js_stream_wrap_emit_close(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item wrap = env[0];
    if (js_property_get(wrap, make_string_item("__stream_wrap_closed__")).item == ITEM_TRUE) {
        return make_undefined_item();
    }
    js_property_set(wrap, make_string_item("__stream_wrap_closed__"), (Item){.item = ITEM_TRUE});
    js_property_set(wrap, make_string_item("readable"), (Item){.item = ITEM_FALSE});
    js_property_set(wrap, make_string_item("writable"), (Item){.item = ITEM_FALSE});
    js_property_set(wrap, make_string_item("destroyed"), (Item){.item = ITEM_TRUE});
    socket_emit(wrap, "close", NULL, 0);
    return make_undefined_item();
}

static bool js_stream_wrap_terminal_event_observed(Item self, Item event_item) {
    if (get_type_id(event_item) != LMD_TYPE_STRING) return false;
    String* ev = it2s(event_item);
    if (!ev) return false;
    if (ev->len == 3 && memcmp(ev->chars, "end", 3) == 0) {
        return js_property_get(self, make_string_item("__stream_wrap_ended__")).item == ITEM_TRUE;
    }
    if (ev->len == 5 && memcmp(ev->chars, "close", 5) == 0) {
        return js_property_get(self, make_string_item("__stream_wrap_closed__")).item == ITEM_TRUE;
    }
    return false;
}

static Item js_stream_wrap_replay_terminal_listener(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    Item self = env[0];
    Item event_item = env[1];
    Item callback = env[2];
    if (!js_stream_wrap_terminal_event_observed(self, event_item)) return make_undefined_item();
    if (env[3].item == ITEM_TRUE) {
        socket_remove_listener_item(self, event_item, callback);
    }
    js_call_function(callback, self, NULL, 0);
    js_microtask_flush();
    return make_undefined_item();
}

static void js_stream_wrap_queue_terminal_replay(Item self, Item event_item,
                                                 Item callback, bool once) {
    if (!is_callable(callback) || !js_stream_wrap_terminal_event_observed(self, event_item)) return;
    Item* env = js_alloc_env(4);
    env[0] = self;
    env[1] = event_item;
    env[2] = callback;
    env[3] = (Item){.item = b2it(once)};
    // Underlying sockets can emit EOF/close while JSStreamSocket is being
    // constructed; replay terminal state to listeners attached just after new.
    js_next_tick_enqueue(js_new_closure((void*)js_stream_wrap_replay_terminal_listener, 0, env, 4));
}

static Item js_stream_wrap_on(Item event_item, Item callback) {
    Item self = js_get_this();
    socket_add_listener_item(self, event_item, callback, false);
    js_stream_wrap_queue_terminal_replay(self, event_item, callback, false);
    return self;
}

static Item js_stream_wrap_once(Item event_item, Item callback) {
    Item self = js_get_this();
    socket_add_listener_item(self, event_item, callback, true);
    js_stream_wrap_queue_terminal_replay(self, event_item, callback, true);
    return self;
}

static Item js_stream_wrap_destroy(void) {
    Item self = js_get_this();
    if (js_property_get(self, make_string_item("__stream_wrap_closed__")).item == ITEM_TRUE) {
        return self;
    }
    Item stream = js_property_get(self, make_string_item("stream"));
    Item destroy = js_property_get(stream, make_string_item("destroy"));
    if (is_callable(destroy)) {
        js_call_function(destroy, stream, NULL, 0);
    } else {
        Item* env = js_alloc_env(1);
        env[0] = self;
        js_stream_wrap_emit_close((Item){.item = i2it((int64_t)(uintptr_t)env)});
    }
    return self;
}

static Item js_stream_wrap_write_exception_tick(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_undefined_item();
    js_process_emit(make_string_item("uncaughtException"), env[1]);
    Item proto_err = socket_make_error("EPROTO", "write EPROTO");
    socket_emit(env[0], "error", &proto_err, 1);
    return make_undefined_item();
}

static void js_stream_wrap_schedule_write_exception(Item self, Item err) {
    Item* env = js_alloc_env(2);
    env[0] = self;
    env[1] = err;
    js_next_tick_enqueue(js_new_closure((void*)js_stream_wrap_write_exception_tick, 0, env, 2));
}

static bool js_stream_wrap_call_direct_write(Item self, Item stream, Item chunk,
                                             Item encoding, Item callback) {
    Item write_fn = js_property_get(stream, make_string_item("_write"));
    if (!is_callable(write_fn)) {
        write_fn = js_property_get(stream, make_string_item("__write_handler__"));
    }
    if (!is_callable(write_fn)) return false;

    Item args[3] = { chunk, encoding, callback };
    js_call_function(write_fn, stream, args, 3);
    if (js_check_exception()) {
        // JSStreamSocket write dispatch must preserve synchronous wrapped-stream
        // throws; the generic Writable path records them as stream errors instead.
        Item err = js_clear_exception();
        js_stream_wrap_schedule_write_exception(self, err);
    }
    return true;
}

static Item js_stream_wrap_write(Item chunk, Item encoding, Item callback) {
    Item self = js_get_this();
    Item stream = js_property_get(self, make_string_item("stream"));
    if (is_callable(encoding) && !is_callable(callback)) {
        callback = encoding;
        encoding = make_undefined_item();
    }
    if (encoding.item == 0) encoding = make_undefined_item();
    if (callback.item == 0) callback = make_undefined_item();

    if (js_stream_wrap_call_direct_write(self, stream, chunk, encoding, callback)) {
        return (Item){.item = ITEM_TRUE};
    }

    Item write_fn = js_property_get(stream, make_string_item("write"));
    if (!is_callable(write_fn)) return (Item){.item = ITEM_FALSE};
    Item args[3] = { chunk, encoding, callback };
    return js_call_function(write_fn, stream, args, 3);
}

static Item js_stream_wrap_end(Item chunk, Item encoding, Item callback) {
    Item self = js_get_this();
    Item stream = js_property_get(self, make_string_item("stream"));
    if (is_callable(chunk)) {
        callback = chunk;
        chunk = make_undefined_item();
        encoding = make_undefined_item();
    } else if (is_callable(encoding) && !is_callable(callback)) {
        callback = encoding;
        encoding = make_undefined_item();
    }
    if (encoding.item == 0) encoding = make_undefined_item();
    if (callback.item == 0) callback = make_undefined_item();

    if (!is_undefined_item(chunk) && chunk.item != ITEM_NULL &&
        js_stream_wrap_call_direct_write(self, stream, chunk, encoding, make_undefined_item())) {
        return self;
    }

    Item end_fn = js_property_get(stream, make_string_item("end"));
    if (is_callable(end_fn)) {
        Item args[3] = { chunk, encoding, callback };
        js_call_function(end_fn, stream, args, 3);
    }
    return self;
}

extern "C" Item js_internal_js_stream_socket_constructor(Item stream) {
    Item wrap = js_new_object();
    Item socket_proto = js_net_get_socket_prototype();
    if (get_type_id(socket_proto) == LMD_TYPE_MAP) {
        js_set_prototype(wrap, socket_proto);
    }
    js_property_set(wrap, make_string_item("stream"), stream);
    js_property_set(wrap, make_string_item("readable"), js_property_get(stream, make_string_item("readable")));
    js_property_set(wrap, make_string_item("writable"), js_property_get(stream, make_string_item("writable")));
    js_property_set(wrap, make_string_item("destroyed"), (Item){.item = ITEM_FALSE});
    js_property_set(wrap, make_string_item("__stream_wrap_ended__"), (Item){.item = ITEM_FALSE});
    js_property_set(wrap, make_string_item("__stream_wrap_closed__"), (Item){.item = ITEM_FALSE});
    js_property_set(wrap, make_string_item("on"), js_new_function((void*)js_stream_wrap_on, 2));
    js_property_set(wrap, make_string_item("once"), js_new_function((void*)js_stream_wrap_once, 2));
    js_property_set(wrap, make_string_item("removeListener"),
                    js_new_function((void*)js_socket_removeListener, 2));
    js_property_set(wrap, make_string_item("off"),
                    js_new_function((void*)js_socket_removeListener, 2));
    js_property_set(wrap, make_string_item("destroy"), js_new_function((void*)js_stream_wrap_destroy, 0));
    js_property_set(wrap, make_string_item("write"), js_new_function((void*)js_stream_wrap_write, 3));
    js_property_set(wrap, make_string_item("end"), js_new_function((void*)js_stream_wrap_end, 3));

    Item on = js_property_get(stream, make_string_item("on"));
    if (is_callable(on)) {
        Item* env = js_alloc_env(1);
        env[0] = wrap;
        // JSStreamSocket is a view over a real stream; missing these mirrors
        // leaves destroy EOF/close tests waiting on unobservable terminal events.
        Item args[2] = { make_string_item("error"),
                         js_new_closure((void*)js_stream_wrap_emit_error, 1, env, 1) };
        js_call_function(on, stream, args, 2);
        args[0] = make_string_item("end");
        args[1] = js_new_closure((void*)js_stream_wrap_emit_end, 0, env, 1);
        js_call_function(on, stream, args, 2);
        args[0] = make_string_item("close");
        args[1] = js_new_closure((void*)js_stream_wrap_emit_close, 0, env, 1);
        js_call_function(on, stream, args, 2);
    }

    return wrap;
}

static Item internal_js_stream_socket_ctor = {0};
static bool internal_js_stream_socket_rooted = false;

extern "C" Item js_get_internal_js_stream_socket_constructor(void) {
    if (internal_js_stream_socket_ctor.item != 0) return internal_js_stream_socket_ctor;
    internal_js_stream_socket_ctor =
        js_new_function((void*)js_internal_js_stream_socket_constructor, 1);
    js_property_set(internal_js_stream_socket_ctor, make_string_item("StreamWrap"),
                    internal_js_stream_socket_ctor);
    js_property_set(internal_js_stream_socket_ctor, make_string_item("default"),
                    internal_js_stream_socket_ctor);
    if (!internal_js_stream_socket_rooted) {
        heap_register_gc_root(&internal_js_stream_socket_ctor.item);
        internal_js_stream_socket_rooted = true;
    }
    return internal_js_stream_socket_ctor;
}

// =============================================================================
// net Module Namespace
// =============================================================================

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
    int parsed_timeout = 0;
    if (!net_parse_auto_select_timeout(timeout_item,
            "defaultAutoSelectFamilyAttemptTimeout", &parsed_timeout)) {
        return ItemNull;
    }
    net_auto_select_family_timeout = parsed_timeout;
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

    net_apply_cli_options();

    net_namespace = js_new_object();

    Item create_server_fn = net_set_method(net_namespace, "createServer", (void*)js_net_createServer, -1);
    net_set_method(net_namespace, "createConnection", (void*)js_net_createConnection, -1);
    net_set_method(net_namespace, "connect",          (void*)js_net_createConnection, -1); // alias
    Item socket_fn = net_set_method(net_namespace, "Socket", (void*)js_net_Socket, 1);
    Item block_list_fn = net_set_method(net_namespace, "BlockList", (void*)js_net_BlockList, 1);
    Item bound_socket_fn = net_set_method(net_namespace, "BoundSocket", (void*)js_net_BoundSocket, 1);
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
    net_constructor_prototype(bound_socket_fn, JS_CLASS_OBJECT);

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

extern "C" Item js_net_get_socket_prototype(void) {
    if (get_type_id(net_socket_prototype) != LMD_TYPE_MAP) {
        js_get_net_namespace();
    }
    return net_socket_prototype;
}

extern "C" void js_net_reset(void) {
    net_namespace = (Item){0};
    internal_js_stream_socket_ctor = (Item){0};
    net_socket_prototype = (Item){0};
    net_server_prototype = (Item){0};
    net_socket_connect_fn = (Item){0};
    memset(net_active_sockets, 0, sizeof(net_active_sockets));
    memset(net_active_servers, 0, sizeof(net_active_servers));
    net_default_auto_select_family = false;
    net_auto_select_family_timeout = 500;
    net_cli_options_applied = false;
    memset(net_block_list_instances, 0, sizeof(net_block_list_instances));
    net_block_list_instance_count = 0;
}
