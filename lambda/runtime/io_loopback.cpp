// io_loopback.cpp — lambda.io.loopback: one HTTP request on 127.0.0.1, the
// redirect that ends a sign-in in the system browser (RFC 8252 §7.3; D7.2.4v4;
// vibe/Lambda_Demo_Mail.md MG7, M4).
//
// `listen` binds an ephemeral port on the loopback interface only. `accept`
// parks the calling task on the event loop, not a pool thread, since a person
// may take minutes to sign in. It answers the browser's request with a short
// page and closes the listener. A connection that sends no request (a
// browser's speculative one) and a favicon request are answered or dropped,
// and the wait goes on. A listener also closes when the task that opened it
// ends.

#include "io_file_search.hpp"
#include "concurrency.h"
#include "lambda-number-runtime.hpp"
#include "re2_wrapper.hpp"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/strbuf.h"
#include "../../lib/time_util.h"
#include "../../lib/url.h"

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET LoopSocket;
#define LOOP_INVALID_SOCKET INVALID_SOCKET
static void loop_socket_close(LoopSocket s) { closesocket(s); }
static bool loop_socket_nonblocking(LoopSocket s) { u_long on = 1; return ioctlsocket(s, FIONBIO, &on) == 0; }
static bool loop_would_block(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static bool loop_sockets_ready(void) {
    static bool ready = [] { WSADATA data; return WSAStartup(MAKEWORD(2, 2), &data) == 0; }();
    return ready;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int LoopSocket;
#define LOOP_INVALID_SOCKET (-1)
static void loop_socket_close(LoopSocket s) { close(s); }
static bool loop_socket_nonblocking(LoopSocket s) {
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
static bool loop_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
static bool loop_sockets_ready(void) { return true; }
#endif

extern __thread EvalContext* context;
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);

#define LOOP_REQUEST_MAX 16384        // the request line and headers; a redirect needs far less
#define LOOP_CONNECTION_WAIT_MS 10000 // a connection that sends nothing this long is dropped
#define LOOP_ACCEPT_MAX_S 3600

static const char LOOP_DEFAULT_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Signed in</title></head>"
    "<body style=\"font:16px -apple-system,sans-serif;margin:3em\"><h1>Done</h1>"
    "<p>You can close this window and return to Lambda.</p></body></html>";

struct Loopback {
    LoopSocket fd;
    int port;
    LambdaSocketWaiter* waiter;
    Item handle;                // the listener's VMap, rooted while open
    bool handle_rooted;
    bool busy;
    bool closed;
    bool release_when_idle;     // the owner ended mid-accept: free when it returns
};

static char loopback_brand;

static Item loopback_handle_get(void* data, Item key) {
    Loopback* l = (Loopback*)data;
    if (!l || l->closed || !is_text_type_id(get_type_id(key))) return ItemNull;
    if (key.get_len() == 4 && !memcmp(key.get_chars(), "port", 4)) return (Item){.item = i2it((int64_t)l->port)};
    return ItemNull;
}
static void loopback_handle_set(void* data, Item key, Item value) {
    (void)data; (void)key; (void)value;
    log_error("io.loopback: listeners are immutable");
}
static int64_t loopback_handle_count(void* data) { (void)data; return 0; }
static SymbolKeyList* loopback_handle_keys(void* data) { (void)data; return NULL; }
static Item loopback_handle_at(void* data, int64_t index) { (void)data; (void)index; return ItemNull; }
// the owning task releases the listener; the handle owns nothing
static void loopback_handle_destroy(void* data) { (void)data; }
static void loopback_handle_trace(void* data, gc_heap* gc) { (void)data; (void)gc; }

static VMapVtable loopback_handle_vtable = {
    loopback_handle_get, loopback_handle_set, loopback_handle_count, loopback_handle_keys,
    loopback_handle_at, loopback_handle_at, loopback_handle_destroy, loopback_handle_trace,
};

static Loopback* loopback_of(Item item) {
    if (get_type_id(item) != LMD_TYPE_VMAP || !item.vmap || item.vmap->host_type != &loopback_brand) return NULL;
    return (Loopback*)item.vmap->host_data;   // NULL once closed
}

static void loopback_close(Loopback* l) {
    if (l->closed) return;
    l->closed = true;
    if (get_type_id(l->handle) == LMD_TYPE_VMAP && l->handle.vmap) {
        l->handle.vmap->data = NULL;
        l->handle.vmap->host_data = NULL;
    }
    if (l->handle_rooted && context && context->heap) heap_unregister_gc_root(&l->handle.item);
    l->handle_rooted = false;
    l->handle = ItemNull;
    // closing the waiter wakes an accept parked on it
    lambda_socket_waiter_close(l->waiter);
    l->waiter = NULL;
    if (!l->busy && l->fd != LOOP_INVALID_SOCKET) {
        loop_socket_close(l->fd);
        l->fd = LOOP_INVALID_SOCKET;
    }
}

static void loopback_free(Loopback* l) {
    if (l->fd != LOOP_INVALID_SOCKET) loop_socket_close(l->fd);
    mem_free(l);
}

static void loopback_owner_done(LambdaTask* task, Item result, void* data) {
    (void)task; (void)result; (void)data;
}

// The task that opened the listener ended, or was torn down.
static void loopback_owner_release(void* data) {
    Loopback* l = (Loopback*)data;
    loopback_close(l);
    if (l->busy) l->release_when_idle = true;
    else loopback_free(l);
}

// listen(): a listener on an ephemeral port of 127.0.0.1; its `port` names it
extern "C" Item pn_io_loopback_listen(void) {
    const char* fn = "io.loopback.listen";
    if (!loop_sockets_ready()) return io_fs_error(ERR_NETWORK_ERROR, "%s: sockets are unavailable", fn);
    LambdaTask* owner = lambda_task_running();
    if (!owner) return io_fs_error(ERR_INVALID_STATE, "%s: a listener needs a running procedure", fn);
    LoopSocket fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == LOOP_INVALID_SOCKET) return io_fs_error(ERR_NETWORK_ERROR, "%s: cannot create a socket", fn);
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // never another interface
    address.sin_port = 0;
    socklen_t length = sizeof(address);
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) != 0 || listen(fd, 4) != 0 ||
        getsockname(fd, (struct sockaddr*)&address, &length) != 0 || !loop_socket_nonblocking(fd)) {
        loop_socket_close(fd);
        return io_fs_error(ERR_NETWORK_ERROR, "%s: cannot listen on 127.0.0.1", fn);
    }
    Loopback* l = (Loopback*)mem_calloc(1, sizeof(Loopback), MEM_CAT_NETWORK);
    if (!l) {
        loop_socket_close(fd);
        return io_fs_error(ERR_OUT_OF_MEMORY, "%s: out of memory", fn);
    }
    l->fd = fd;
    l->port = ntohs(address.sin_port);
    l->waiter = lambda_socket_waiter_new((uint64_t)fd);
    if (!l->waiter || !lambda_task_on_complete(owner, loopback_owner_done, l, loopback_owner_release)) {
        lambda_socket_waiter_close(l->waiter);
        loopback_free(l);
        return io_fs_error(ERR_INVALID_STATE, "%s: cannot wait on the listener", fn);
    }
    VMap* handle = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    if (!handle) {
        loopback_close(l);
        return io_fs_error(ERR_OUT_OF_MEMORY, "%s: out of memory", fn);
    }
    handle->type_id = LMD_TYPE_VMAP;
    handle->vtable = &loopback_handle_vtable;
    handle->host_type = &loopback_brand;
    handle->host_data = l;
    handle->data = l;
    l->handle = (Item){.vmap = handle};
    heap_register_gc_root(&l->handle.item);
    l->handle_rooted = true;
    log_debug("io.loopback: listening on 127.0.0.1:%d", l->port);
    return l->handle;
}

// Reads one request head into `head`: 1 when complete, 0 when the peer sent
// none in time or closed early, -1 with *error set when the task was cancelled.
static int loopback_read_head(LoopSocket client, int64_t wait_ms, StrBuf* head, Item* error) {
    LambdaSocketWaiter* waiter = lambda_socket_waiter_new((uint64_t)client);
    if (!waiter) return 0;
    uint64_t deadline = time_now_ms() + (uint64_t)wait_ms;
    int outcome = 0;
    while (head->length < LOOP_REQUEST_MAX) {
        char chunk[2048];
        long n = (long)recv(client, chunk, (int)sizeof(chunk), 0);
        if (n > 0) {
            strbuf_append_str_n(head, chunk, (size_t)n);
            if (strstr(head->str, "\r\n\r\n") || strstr(head->str, "\n\n")) { outcome = 1; break; }
            continue;
        }
        if (n == 0 || !loop_would_block()) break;
        uint64_t now = time_now_ms();
        if (now >= deadline) break;
        int ready = lambda_socket_waiter_wait(waiter, false, (int64_t)(deadline - now), error);
        if (ready < 0) { outcome = -1; break; }
        if (ready == 0) break;
    }
    lambda_socket_waiter_close(waiter);
    return outcome;
}

static void loopback_respond(LoopSocket client, const char* status, const char* body, size_t body_length) {
    StrBuf* reply = strbuf_new_cap(256 + body_length);
    if (!reply) return;
    strbuf_append_format(reply, "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n", status, body_length);
    strbuf_append_str_n(reply, body, body_length);
    // a short reply to a waiting browser: a few tries cover a full send buffer
    size_t sent = 0;
    for (int tries = 0; sent < reply->length && tries < 1000; tries++) {
        long n = (long)send(client, reply->str + sent, (int)(reply->length - sent), 0);
        if (n > 0) sent += (size_t)n;
        else if (!loop_would_block()) break;
    }
    strbuf_free(reply);
}

// "GET /path?query HTTP/1.1" into its method, path and decoded query items
static Item loopback_request_item(const char* head) {
    const char* line_end = strpbrk(head, "\r\n");
    size_t line_length = line_end ? (size_t)(line_end - head) : strlen(head);
    const char* sp1 = (const char*)memchr(head, ' ', line_length);
    const char* sp2 = sp1 ? (const char*)memchr(sp1 + 1, ' ', line_length - (size_t)(sp1 + 1 - head)) : NULL;
    const char* target = sp1 ? sp1 + 1 : head;
    size_t target_length = sp2 ? (size_t)(sp2 - target) : (sp1 ? line_length - (size_t)(target - head) : 0);
    const char* question = (const char*)memchr(target, '?', target_length);
    size_t path_length = question ? (size_t)(question - target) : target_length;

    static const char* request_names[] = {"method", "path", "query"};
    static const TypeId request_types[] = {LMD_TYPE_STRING, LMD_TYPE_STRING, LMD_TYPE_ARRAY};
    static const char* pair_names[] = {"name", "value"};
    static const TypeId pair_types[] = {LMD_TYPE_STRING, LMD_TYPE_STRING};
    RootFrame roots(5);
    Rooted<Array*> query(roots, array());
    Rooted<Item> name(roots, ItemNull), value(roots, ItemNull);
    Rooted<Map*> map(roots, (Map*)NULL);
    Rooted<Item> method(roots, io_fs_make_string(head, sp1 ? (size_t)(sp1 - head) : 0));
    if (question) {
        const char* p = question + 1;
        const char* end = target + target_length;
        while (p < end) {
            const char* amp = (const char*)memchr(p, '&', (size_t)(end - p));
            const char* stop = amp ? amp : end;
            const char* eq = (const char*)memchr(p, '=', (size_t)(stop - p));
            if (stop > p) {
                size_t name_length = 0, value_length = 0;
                char* decoded_name = url_decode_form(p, (size_t)((eq ? eq : stop) - p), &name_length);
                char* decoded_value = eq ? url_decode_form(eq + 1, (size_t)(stop - eq - 1), &value_length) : NULL;
                name.set(io_fs_make_string(decoded_name ? decoded_name : "", name_length));
                value.set(io_fs_make_string(decoded_value ? decoded_value : "", value_length));
                mem_free(decoded_name);
                mem_free(decoded_value);
                map.set(map_alloc_for_type(runtime_result_shape(pair_names, pair_types, 2), NULL, 0));
                if (!map.get()) return ItemError;
                Item pair[] = {name.get(), value.get()};
                map_fill_items(map.get(), pair, 2);
                array_push_verbatim(query.get(), (Item){.map = map.get()});
            }
            p = stop + 1;
        }
    }
    size_t decoded_path_length = 0;
    char* decoded_path = url_decode_component(target, path_length, &decoded_path_length);
    Rooted<Item> path(roots, io_fs_make_string(decoded_path ? decoded_path : "", decoded_path_length));
    mem_free(decoded_path);
    map.set(map_alloc_for_type(runtime_result_shape(request_names, request_types, 3), NULL, 0));
    if (!map.get()) return ItemError;
    Item values[] = {method.get(), path.get(), (Item){.array = query.get()}};
    map_fill_items(map.get(), values, 3);
    return (Item){.map = map.get()};
}

// accept(listener, seconds, page): the browser's request as {method, path,
// query: [{name, value}]}, after answering it with `page` (HTML; null gives a
// short "you can close this window" page). The listener then closes; a
// timeout leaves it open for another accept.
extern "C" Item pn_io_loopback_accept(Item listener, Item seconds, Item page) {
    const char* fn = "io.loopback.accept";
    Loopback* l = loopback_of(listener);
    if (!l || l->closed) return io_fs_error(ERR_INVALID_STATE, "%s: not an open loopback listener", fn);
    if (l->busy) return io_fs_error(ERR_INVALID_STATE, "%s: the listener is already waiting", fn);
    int64_t wait_s = 0;
    if (!lambda_item_to_int64_exact(seconds, &wait_s) || wait_s < 1 || wait_s > LOOP_ACCEPT_MAX_S) {
        return io_fs_error(ERR_TYPE_MISMATCH, "%s: seconds must be an int from 1 to %d", fn, LOOP_ACCEPT_MAX_S);
    }
    TypeId page_type = get_type_id(page);
    if (page_type != LMD_TYPE_NULL && !is_text_type_id(page_type)) {
        return io_fs_error(ERR_TYPE_MISMATCH, "%s: the page must be HTML text or null", fn);
    }
    const char* body = page_type == LMD_TYPE_NULL ? LOOP_DEFAULT_PAGE : page.get_chars();
    size_t body_length = page_type == LMD_TYPE_NULL ? sizeof(LOOP_DEFAULT_PAGE) - 1 : page.get_len();
    l->busy = true;
    uint64_t deadline = time_now_ms() + (uint64_t)wait_s * 1000;
    Item result = ItemNull;
    StrBuf* head = strbuf_new_cap(1024);
    while (head) {
        uint64_t now = time_now_ms();
        if (now >= deadline || l->closed) {
            result = l->closed ? io_fs_error(ERR_CANCELLED, "%s: the listener closed", fn)
                : io_fs_error(ERR_NETWORK_TIMEOUT, "%s: no request within %lld seconds", fn, (long long)wait_s);
            break;
        }
        Item error = ItemNull;
        int ready = lambda_socket_waiter_wait(l->waiter, false, (int64_t)(deadline - now), &error);
        if (ready < 0) { result = error; break; }
        if (ready == 0) continue;
        LoopSocket client = accept(l->fd, NULL, NULL);
        if (client == LOOP_INVALID_SOCKET) continue;
        loop_socket_nonblocking(client);
        strbuf_reset(head);
        uint64_t left = deadline - time_now_ms();
        int got = loopback_read_head(client, left < LOOP_CONNECTION_WAIT_MS ? (int64_t)left : LOOP_CONNECTION_WAIT_MS,
                                     head, &error);
        if (got < 0) { loop_socket_close(client); result = error; break; }
        if (got == 0) { loop_socket_close(client); continue; }
        if (!strncmp(head->str, "GET /favicon.ico", 16)) {
            loopback_respond(client, "404 Not Found", "", 0);
            loop_socket_close(client);
            continue;
        }
        loopback_respond(client, "200 OK", body, body_length);
        loop_socket_close(client);
        result = loopback_request_item(head->str);
        // one request: the listener has served its purpose
        loopback_close(l);
        break;
    }
    if (head) strbuf_free(head);
    else result = io_fs_error(ERR_OUT_OF_MEMORY, "%s: out of memory", fn);
    l->busy = false;
    if (l->closed && l->fd != LOOP_INVALID_SOCKET) {
        loop_socket_close(l->fd);
        l->fd = LOOP_INVALID_SOCKET;
    }
    if (l->release_when_idle) loopback_free(l);
    return result;
}
