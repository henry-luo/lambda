// lambda.io.websocket (D7.2.4v4): task-owned RFC 6455 connections. curl owns
// client TCP/TLS setup; socket readiness parks tasks, never the UI or a worker.
#include "io_file_search.hpp"
#include "io_resource.hpp"
#include "io_websocket.hpp"
#include "concurrency.h"
#include "lambda-number-runtime.hpp"
#include "re2_wrapper.hpp"
#include "sys_crypto.hpp"
#include "../network/curl_trust.h"
#include "../network/cookie_jar.h"
#include "../network/socket_utils.h"
#include "../../lib/base64.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/time_util.h"
#include "../../lib/utf.h"
#include "../../lib/uv_loop.h"
#include <curl/curl.h>
#include <mbedtls/sha1.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#else
#define strncasecmp _strnicmp
#endif

extern __thread EvalContext* context;
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);

static const size_t WS_HEADER_MAX = 16384;
static const int64_t WS_MESSAGE_MAX = 16 * 1024 * 1024;
static const int64_t WS_CLOSE_TIMEOUT_MS = 5000;
static char websocket_brand;

struct WebSocket {
    CURL* curl;
    NetworkSocket socket;
    LambdaSocketWaiter* waiter;
    StrBuf* input;
    StrBuf* fragments;
    StrBuf* pong;
    StrBuf* close_payload;
    ArrayList* protocols;
    ArrayList* origins;
    char* protocol;
    uv_timer_t* close_timer;
    LambdaTask* close_waiter;
    Item handle;
    int64_t timeout_ms;
    size_t max_bytes;
    unsigned active;
    unsigned fragment_opcode;
    int port;
    bool client;
    bool listener;
    bool reading;
    bool writing;
    bool closed;
    bool rooted;
    bool owner_released;
    bool close_sent;
    bool closing;
    bool close_received;
    bool pong_pending;
};

static Item ws_error(const char* text) {
    return io_fs_error(ERR_NETWORK_ERROR, "io.websocket: %s", text);
}

static bool ws_message_limit(IoFsOptionMap* options, int64_t* bytes, Item* error) {
    if (!io_fs_option_count(options, "max_message_bytes", bytes, error)) return false;
    if (*bytes >= 1 && *bytes <= WS_MESSAGE_MAX) return true;
    *error = ws_error("max_message_bytes must be from 1 to 16777216");
    return false;
}

static WebSocket* ws_of(Item item) {
    if (get_type_id(item) != LMD_TYPE_VMAP || !item.vmap || item.vmap->host_type != &websocket_brand) return NULL;
    return (WebSocket*)item.vmap->host_data;
}

static void ws_dispose(WebSocket* s) {
    lambda_socket_waiter_close(s->waiter);
    s->waiter = NULL;
    if (s->curl) { curl_easy_cleanup(s->curl); s->curl = NULL; }
    else if (s->socket != NETWORK_INVALID_SOCKET) network_socket_close(s->socket);
    s->socket = NETWORK_INVALID_SOCKET;
}

static void ws_invalidate(WebSocket* s) {
    if (s->closed) return;
    s->closed = true;
    if (s->close_timer) {
        uv_timer_stop(s->close_timer);
        uv_close((uv_handle_t*)s->close_timer, [](uv_handle_t* h) { mem_free(h); });
        s->close_timer = NULL;
    }
    if (s->close_waiter) lambda_task_resume(s->close_waiter);
    // invalidate the rooted carrier before releasing native state (D5.3.3).
    if (get_type_id(s->handle) == LMD_TYPE_VMAP && s->handle.vmap) { s->handle.vmap->data = NULL; s->handle.vmap->host_data = NULL; }
    if (s->rooted) heap_unregister_gc_root(&s->handle.item);
    s->rooted = false;
    s->handle = ItemNull;
    lambda_socket_waiter_close(s->waiter);
    s->waiter = NULL;
    if (!s->active) ws_dispose(s);
}

static void ws_free(WebSocket* s) {
    ws_dispose(s);
    strbuf_free(s->input);
    strbuf_free(s->fragments);
    strbuf_free(s->pong);
    strbuf_free(s->close_payload);
    io_fs_text_list_free(s->protocols);
    io_fs_text_list_free(s->origins);
    mem_free(s->protocol);
    mem_free(s);
}

static void ws_owner_done(LambdaTask* task, Item result, void* data) { (void)task; (void)result; (void)data; }
static void ws_owner_release(void* data) {
    WebSocket* s = (WebSocket*)data;
    ws_invalidate(s);
    s->owner_released = true;
    if (!s->active) ws_free(s);
}

static Item ws_handle_get(void* data, Item key) {
    WebSocket* s = (WebSocket*)data;
    if (s && s->listener && is_text_type_id(get_type_id(key)) && key.get_len() == 4 && !memcmp(key.get_chars(), "port", 4))
        return (Item){.item = i2it(s->port)};
    if (s && !s->listener && is_text_type_id(get_type_id(key)) && key.get_len() == 8 && !memcmp(key.get_chars(), "protocol", 8))
        return io_fs_make_string(s->protocol ? s->protocol : "", s->protocol ? strlen(s->protocol) : 0);
    return ItemNull;
}
static VMapVtable ws_vtable = {ws_handle_get, io_resource_set, io_resource_count, io_resource_keys,
    io_resource_at, io_resource_at, io_resource_destroy, io_resource_trace};

static WebSocket* ws_new(void) {
    WebSocket* s = (WebSocket*)mem_calloc(1, sizeof(WebSocket), MEM_CAT_NETWORK);
    if (!s) return NULL;
    s->socket = NETWORK_INVALID_SOCKET;
    s->handle = ItemNull;
    s->input = strbuf_new_cap(1024);
    s->fragments = strbuf_new_cap(1024);
    s->pong = strbuf_new_cap(126);
    s->close_payload = strbuf_new_cap(126);
    s->timeout_ms = 30000;
    s->max_bytes = 1024 * 1024;
    if (!s->input || !s->fragments || !s->pong || !s->close_payload) { ws_free(s); return NULL; }
    return s;
}

static Item ws_publish(WebSocket* s) {
    LambdaTask* owner = lambda_task_running();
    if (!owner || !lambda_task_on_complete(owner, ws_owner_done, s, ws_owner_release)) {
        ws_free(s);
        return io_fs_error(ERR_INVALID_STATE, "io.websocket: acquisition requires a running procedure");
    }
    VMap* handle = io_resource_handle_new(&ws_vtable, &websocket_brand, s);
    if (!handle) { ws_invalidate(s); return ItemError; }
    s->handle = (Item){.vmap = handle};
    heap_register_gc_root(&s->handle.item);
    s->rooted = true;
    return s->handle;
}

static WebSocket* ws_begin(Item handle, bool writing, bool listener, Item* error) {
    WebSocket* s = ws_of(handle);
    if (!s || s->closed || s->listener != listener) {
        *error = io_fs_error(ERR_INVALID_STATE, "io.websocket: expected an open %s", listener ? "listener" : "connection");
        return NULL;
    }
    bool* busy = writing ? &s->writing : &s->reading;
    if (*busy) { *error = io_fs_error(ERR_INVALID_STATE, "io.websocket: another %s is pending", writing ? "writer" : "reader"); return NULL; }
    *busy = true;
    s->active++;
    return s;
}
static void ws_end(WebSocket* s, bool writing) {
    (writing ? s->writing : s->reading) = false;
    s->active--;
    if (writing && s->close_waiter) lambda_task_resume(s->close_waiter);
    if (s->closed && !s->active) {
        if (s->owner_released) ws_free(s);
        else ws_dispose(s);
    }
}

// a single deadline covers all partial reads/writes, including control frames.
static uint64_t ws_deadline(int64_t ms) { return ms > 0 ? time_now_ms() + (uint64_t)ms : 0; }
static bool ws_wait(WebSocket* s, bool writing, uint64_t deadline, Item* error) {
    if (s->closed) { *error = ws_error("connection closed"); return false; }
    uint64_t now = time_now_ms();
    if (deadline && now >= deadline) { *error = io_fs_error(ERR_NETWORK_TIMEOUT, "io.websocket: operation timed out"); return false; }
    int ready = lambda_socket_waiter_wait(s->waiter, writing, deadline ? (int64_t)(deadline - now) : 0, error);
    if (ready > 0) return !s->closed;
    if (ready == 0) *error = io_fs_error(ERR_NETWORK_TIMEOUT, "io.websocket: operation timed out");
    return false;
}

static bool ws_write(WebSocket* s, const char* data, size_t length, uint64_t deadline, Item* error) {
    size_t done = 0;
    while (done < length) {
        size_t written = 0;
        bool again = false;
        if (s->curl) {
            CURLcode code = curl_easy_send(s->curl, data + done, length - done, &written);
            again = code == CURLE_AGAIN;
            if (code != CURLE_OK && !again) { *error = ws_error(curl_easy_strerror(code)); return false; }
        } else {
            long n = network_socket_send(s->socket, data + done, length - done);
            if (n > 0) written = (size_t)n;
            else if (n < 0 && network_socket_would_block()) again = true;
            else { *error = ws_error("socket write failed"); return false; }
        }
        done += written;
        if ((again || !written) && !ws_wait(s, true, deadline, error)) return false;
    }
    return true;
}

static bool ws_read_more(WebSocket* s, uint64_t deadline, size_t limit, Item* error) {
    while (s->input->length < limit) {
        if (deadline && time_now_ms() >= deadline) {
            *error = io_fs_error(ERR_NETWORK_TIMEOUT, "io.websocket: operation timed out"); return false;
        }
        char bytes[8192];
        size_t room = limit - s->input->length;
        if (room > sizeof(bytes)) room = sizeof(bytes);
        size_t received = 0;
        bool again = false;
        if (s->curl) {
            CURLcode code = curl_easy_recv(s->curl, bytes, room, &received);
            again = code == CURLE_AGAIN;
            if (code != CURLE_OK && !again) { *error = ws_error(curl_easy_strerror(code)); return false; }
        } else {
            long n = (long)recv(s->socket, bytes, (int)room, 0);
            if (n > 0) received = (size_t)n;
            else if (n < 0 && network_socket_would_block()) again = true;
        }
        if (received) {
            if (!strbuf_ensure_cap(s->input, s->input->length + received + 1)) { *error = ItemError; return false; }
            strbuf_append_str_n(s->input, bytes, received);
            return true;
        }
        if (!again) { *error = ws_error("peer disconnected without a close frame"); return false; }
        if (!ws_wait(s, false, deadline, error)) return false;
    }
    *error = ws_error("input exceeds its byte limit");
    return false;
}

static void ws_consume(StrBuf* buf, size_t n) {
    memmove(buf->str, buf->str + n, buf->length - n);
    buf->length -= n;
    buf->str[buf->length] = 0;
}

static bool ws_frame(WebSocket* s, unsigned opcode, const char* data, size_t length, Item* error) {
    StrBuf* frame = strbuf_new_cap(length + 15);
    if (!frame) { *error = ItemError; return false; }
    unsigned char head[14] = {(unsigned char)(0x80 | opcode), (unsigned char)(s->client ? 0x80 : 0)};
    size_t n = 2;
    if (length < 126) head[1] |= (unsigned char)length;
    else if (length <= 65535) {
        head[1] |= 126; head[n++] = (unsigned char)(length >> 8); head[n++] = (unsigned char)length;
    } else {
        head[1] |= 127;
        for (int i = 7; i >= 0; i--) head[n++] = (unsigned char)((uint64_t)length >> (i * 8));
    }
    unsigned char mask[4] = {};
    if (s->client) {
        if (!crypto_random(mask, sizeof(mask))) { strbuf_free(frame); *error = ws_error("no entropy source"); return false; }
        memcpy(head + n, mask, 4); n += 4;
    }
    strbuf_append_str_n(frame, (const char*)head, n);
    strbuf_append_str_n(frame, data, length);
    if (s->client) for (size_t i = 0; i < length; i++) frame->str[n + i] ^= mask[i % 4];
    bool ok = ws_write(s, frame->str, frame->length, ws_deadline(s->timeout_ms), error);
    strbuf_free(frame);
    return ok;
}

bool io_websocket_protocol_token(const char* text) {
    if (!text || !*text) return false;
    for (const unsigned char* p = (const unsigned char*)text; *p; p++)
        if (*p < 33 || *p > 126 || strchr("()<>@,;:\\\"/[]?={} \t", *p)) return false;
    return true;
}

static bool ws_list_has(ArrayList* list, const char* value) {
    if (list) for (int i = 0; i < list->length; i++)
        if (!strcmp((const char*)list->data[i], value)) return true;
    return false;
}

static bool ws_protocols_valid(ArrayList* list) {
    if (list) for (int i = 0; i < list->length; i++) {
        const char* token = (const char*)list->data[i];
        if (!io_websocket_protocol_token(token)) return false;
        for (int j = 0; j < i; j++) if (!strcmp(token, (const char*)list->data[j])) return false;
    }
    return true;
}

static int ws_header(StrBuf* head, const char* name, char* out, size_t cap, bool list = false) {
    const char* p = strstr(head->str, "\r\n");
    if (!p) return false;
    size_t wanted = strlen(name);
    bool found = false;
    size_t used = 0;
    out[0] = 0;
    while (*(p += 2)) {
        const char* end = strstr(p, "\r\n");
        if (!end || end == p) break;
        const char* colon = (const char*)memchr(p, ':', (size_t)(end - p));
        if (colon && (size_t)(colon - p) == wanted && !strncasecmp(p, name, wanted)) {
            if (found && !list) return -1; // singleton handshake fields cannot be combined
            const char* value = colon + 1;
            while (value < end && (*value == ' ' || *value == '\t')) value++;
            while (end > value && (end[-1] == ' ' || end[-1] == '\t')) end--;
            size_t length = (size_t)(end - value);
            if (used + length + (found ? 1 : 0) >= cap) return -1;
            if (found) out[used++] = ',';
            memcpy(out + used, value, length); used += length; out[used] = 0;
            found = true;
        }
        p = strstr(p, "\r\n");
        if (!p) break;
    }
    return found;
}

static bool ws_headers_valid(StrBuf* head) {
    const char* p = strstr(head->str, "\r\n");
    if (!p) return false;
    while (*(p += 2)) {
        const char* end = strstr(p, "\r\n");
        if (!end) return false;
        if (end == p) return true;
        const char* colon = (const char*)memchr(p, ':', (size_t)(end - p));
        if (!colon || colon == p) return false;
        for (const char* c = p; c < end; c++) {
            unsigned char b = (unsigned char)*c;
            if ((c < colon && (b < 33 || b > 126 || strchr("()<>@,;:\\\"/[]?={} \t", b))) ||
                (c > colon && ((b < 32 && b != '\t') || b == 127))) return false;
        }
        p = end;
    }
    return false;
}
static bool ws_header_token(const char* value, const char* token) {
    size_t n = strlen(token);
    while (*value) {
        while (*value == ' ' || *value == '\t' || *value == ',') value++;
        const char* end = strchr(value, ',');
        if (!end) end = value + strlen(value);
        const char* stop = end;
        while (stop > value && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;
        if ((size_t)(stop - value) == n && !strncasecmp(value, token, n)) return true;
        value = end;
    }
    return false;
}
static bool ws_accept_key(const char* key, char accept[32]) {
    StrBuf* input = strbuf_create(key);
    if (!input) return false;
    strbuf_append_str(input, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    unsigned char digest[20];
    bool ok = mbedtls_sha1((const unsigned char*)input->str, input->length, digest) == 0;
    strbuf_free(input);
    if (ok) base64_encode(digest, sizeof(digest), accept, BASE64_STD);
    return ok;
}
static StrBuf* ws_read_head(WebSocket* s, Item* error) {
    uint64_t deadline = ws_deadline(s->timeout_ms);
    while (true) {
        const char* end = strstr(s->input->str, "\r\n\r\n");
        if (end) {
            size_t length = (size_t)(end - s->input->str) + 4;
            if (memchr(s->input->str, 0, length)) { *error = ws_error("NUL in upgrade headers"); return NULL; }
            StrBuf* head = strbuf_new_cap(length + 1);
            if (!head) { *error = ItemError; return NULL; }
            strbuf_append_str_n(head, s->input->str, length);
            ws_consume(s->input, length);
            return head;
        }
        if (!ws_read_more(s, deadline, WS_HEADER_MAX, error)) return NULL;
    }
}

struct WsConnect {
    CURL* curl;
    CURLcode code;
    const int* cancelled;
};
static void ws_connect_run(LambdaOffload* job, void* data) {
    WsConnect* c = (WsConnect*)data;
    c->cancelled = lambda_offload_cancel_flag(job);
    c->code = curl_easy_perform(c->curl);
}
static int ws_connect_progress(void* data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const int* cancelled = ((WsConnect*)data)->cancelled;
    return cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE) ? 1 : 0;
}
static void ws_connect_release(void* data) {
    WsConnect* c = (WsConnect*)data;
    if (c->curl) curl_easy_cleanup(c->curl);
    mem_free(c);
}

static bool ws_read_timeout(Item value, int64_t* timeout, Item* error) {
    if (!lambda_item_to_int64_exact(value, timeout) || *timeout < 0 || *timeout > 3600000) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.websocket: timeout_ms must be an int from 0 to 3600000");
        return false;
    }
    return true;
}

Item io_websocket_open_browser(Item url, Item options, CookieJar* cookies) {
    struct CookieOwner { CookieJar* jar; ~CookieOwner() { cookie_jar_release(jar); } } cookie_owner = {
        cookies && cookie_jar_retain(cookies) ? cookies : NULL};
    cookies = cookie_owner.jar;
    Item error = ItemNull;
    if (!is_text_type_id(get_type_id(url)) || memchr(url.get_chars(), 0, url.get_len()))
        return io_fs_error(ERR_TYPE_MISMATCH, "io.websocket.open: URL must be text without NUL");
    const char* raw = url.get_chars();
    bool secure = !strncasecmp(raw, "wss://", 6) || !strncasecmp(raw, "https://", 8);
    size_t scheme = !strncasecmp(raw, "ws://", 5) ? 5 : !strncasecmp(raw, "wss://", 6) ? 6 :
        !strncasecmp(raw, "http://", 7) ? 7 : !strncasecmp(raw, "https://", 8) ? 8 : 0;
    if (!scheme || memchr(raw, '#', url.get_len())) return ws_error("URL must use ws/wss/http/https and have no fragment");
    IoFsOptionMap opts;
    int64_t timeout = 30000, max_bytes = 1024 * 1024;
    char* ca_file = NULL;
    char* origin = NULL;
    ArrayList* protocols = NULL;
    if (!io_fs_options_open(options, "io.websocket.open", SYSPROC_IO_WEBSOCKET_OPEN, &opts, &error) ||
        !io_fs_option_count(&opts, "timeout_ms", &timeout, &error) ||
        !ws_message_limit(&opts, &max_bytes, &error) ||
        !io_fs_option_text(&opts, "ca_file", &ca_file, &error) ||
        !io_fs_option_text(&opts, "origin", &origin, &error) ||
        !io_fs_option_text_list(&opts, "protocols", &protocols, &error)) {
        mem_free(ca_file); mem_free(origin); io_fs_text_list_free(protocols); return error;
    }
    if (!ws_protocols_valid(protocols) || (origin && (strchr(origin, '\r') || strchr(origin, '\n')))) {
        mem_free(ca_file); mem_free(origin); io_fs_text_list_free(protocols);
        return ws_error("invalid Origin or duplicate/invalid subprotocol");
    }
    // release option storage on every early exit, including cancelled connects.
    struct OptionsOwner {
        char* origin; ArrayList* protocols;
        ~OptionsOwner() { mem_free(origin); io_fs_text_list_free(protocols); }
    } option_owner = {origin, protocols};
    if (timeout < 1 || timeout > 3600000) {
        mem_free(ca_file); return ws_error("timeout_ms must be from 1 to 3600000");
    }
    StrBuf* address = strbuf_create(secure ? "https://" : "http://");
    strbuf_append_str(address, raw + scheme);
    CURLU* parsed = curl_url();
    char *host = NULL, *port = NULL, *path = NULL, *query = NULL, *user = NULL;
    bool valid = parsed && curl_url_set(parsed, CURLUPART_URL, address->str, 0) == CURLUE_OK &&
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
        curl_url_get(parsed, CURLUPART_PATH, &path, 0) == CURLUE_OK &&
        curl_url_get(parsed, CURLUPART_USER, &user, 0) != CURLUE_OK;
    if (!valid) {
        curl_free(host); curl_free(path); curl_free(user); curl_url_cleanup(parsed); strbuf_free(address); mem_free(ca_file);
        return ws_error("invalid URL or embedded credentials");
    }
    curl_url_get(parsed, CURLUPART_PORT, &port, 0);
    curl_url_get(parsed, CURLUPART_QUERY, &query, 0);
    unsigned char nonce[16];
    char key[25], expected[32];
    StrBuf* request = strbuf_new_cap(512);
    bool random = crypto_random(nonce, sizeof(nonce));
    if (random) base64_encode(nonce, sizeof(nonce), key, BASE64_STD);
    if (random && ws_accept_key(key, expected)) {
        strbuf_append_format(request, "GET %s%s%s HTTP/1.1\r\nHost: %s%s%s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n",
            path, query ? "?" : "", query ? query : "", host, port ? ":" : "", port ? port : "", key);
        if (origin) strbuf_append_format(request, "Origin: %s\r\n", origin);
        char* cookie = cookie_jar_build_request_cookie(cookies, address->str);
        if (cookie) strbuf_append_format(request, "Cookie: %s\r\n", cookie);
        mem_free(cookie);
        if (protocols && protocols->length) {
            strbuf_append_str(request, "Sec-WebSocket-Protocol: ");
            for (int i = 0; i < protocols->length; i++)
                strbuf_append_format(request, "%s%s", i ? ", " : "", (char*)protocols->data[i]);
            strbuf_append_str(request, "\r\n");
        }
        strbuf_append_str(request, "\r\n");
    }
    curl_free(host); curl_free(port); curl_free(path); curl_free(query); curl_url_cleanup(parsed);
    if (!request || !request->length) { strbuf_free(request); strbuf_free(address); mem_free(ca_file); return ws_error("cannot generate handshake"); }
    WsConnect* job = (WsConnect*)mem_calloc(1, sizeof(WsConnect), MEM_CAT_NETWORK);
    if (!job) { strbuf_free(request); strbuf_free(address); mem_free(ca_file); return ItemError; }
    job->curl = curl_easy_init();
    if (!job->curl) { mem_free(job); strbuf_free(request); strbuf_free(address); mem_free(ca_file); return ItemError; }
    curl_easy_setopt(job->curl, CURLOPT_URL, address->str);
    curl_easy_setopt(job->curl, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(job->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(job->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(job->curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
    curl_easy_setopt(job->curl, CURLOPT_CONNECTTIMEOUT_MS, (long)timeout);
    curl_easy_setopt(job->curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(job->curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (ca_file) curl_easy_setopt(job->curl, CURLOPT_CAINFO, ca_file);
    else curl_use_host_trust_store(job->curl);
    curl_easy_setopt(job->curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(job->curl, CURLOPT_XFERINFOFUNCTION, ws_connect_progress);
    curl_easy_setopt(job->curl, CURLOPT_XFERINFODATA, job);
    static const LambdaOffloadOps connect_ops = {ws_connect_run, NULL, ws_connect_release};
    Item outcome = lambda_task_offload(&connect_ops, job, 0);
    mem_free(ca_file);
    if (get_type_id(outcome) == LMD_TYPE_ERROR || job->code != CURLE_OK) {
        Item failure = get_type_id(outcome) == LMD_TYPE_ERROR ? outcome : ws_error(curl_easy_strerror(job->code));
        ws_connect_release(job); strbuf_free(request); strbuf_free(address); return failure;
    }
    curl_easy_setopt(job->curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(job->curl, CURLOPT_XFERINFODATA, NULL);
    WebSocket* s = ws_new();
    if (!s) { ws_connect_release(job); strbuf_free(request); strbuf_free(address); return ItemError; }
    s->curl = job->curl; job->curl = NULL; mem_free(job);
    s->client = true; s->timeout_ms = timeout; s->max_bytes = (size_t)max_bytes;
    curl_socket_t fd = CURL_SOCKET_BAD;
    curl_easy_getinfo(s->curl, CURLINFO_ACTIVESOCKET, &fd);
    s->socket = (NetworkSocket)fd;
    s->waiter = lambda_socket_waiter_new((uint64_t)fd);
    RootFrame roots(1);
    Rooted<Item> handle(roots, ws_publish(s));
    if (get_type_id(handle.get()) == LMD_TYPE_ERROR) { strbuf_free(request); strbuf_free(address); return handle.get(); }
    s->active++;
    bool ok = s->waiter && ws_write(s, request->str, request->length, ws_deadline(timeout), &error);
    strbuf_free(request);
    StrBuf* head = ok ? ws_read_head(s, &error) : NULL;
    char upgrade[64], connection[128], accept[64], extensions[128], protocol[WS_HEADER_MAX];
    ok = head && ws_headers_valid(head) && !strncmp(head->str, "HTTP/1.1 101 ", 13) &&
        (ws_header(head, "Upgrade", upgrade, sizeof(upgrade), true) == 1) && ws_header_token(upgrade, "websocket") &&
        (ws_header(head, "Connection", connection, sizeof(connection), true) == 1) && ws_header_token(connection, "upgrade") &&
        (ws_header(head, "Sec-WebSocket-Accept", accept, sizeof(accept)) == 1) && !strcmp(accept, expected) &&
        (ws_header(head, "Sec-WebSocket-Extensions", extensions, sizeof(extensions)) == 0) &&
        (ws_header(head, "Sec-WebSocket-Protocol", protocol, sizeof(protocol)) >= 0) &&
        (protocol[0] ? ws_list_has(protocols, protocol) : !protocols || !protocols->length);
    if (ok) s->protocol = mem_strdup(protocol, MEM_CAT_NETWORK);
    if (head && cookies && ws_headers_valid(head)) {
        // Set-Cookie is repeated rather than comma-combined (RFC 6265).
        const char* p = strstr(head->str, "\r\n");
        while (p && *(p += 2)) {
            const char* end = strstr(p, "\r\n");
            if (!end || p == end) break;
            if ((size_t)(end - p) >= 11 && !strncasecmp(p, "Set-Cookie:", 11)) {
                char* value = mem_dup_n(p + 11, (size_t)(end - p - 11), MEM_CAT_NETWORK);
                if (value) cookie_jar_store(cookies, address->str, value);
                mem_free(value);
            }
            p = end;
        }
        cookie_jar_flush(cookies);
    }
    strbuf_free(address);
    strbuf_free(head);
    if (!ok) {
        ws_invalidate(s);
        ws_end(s, false);
        return get_type_id(error) == LMD_TYPE_ERROR ? error : ws_error("invalid WebSocket upgrade response");
    }
    ws_end(s, false);
    return handle.get();
}
extern "C" Item pn_io_websocket_open2(Item url, Item options) { return io_websocket_open_browser(url, options, NULL); }
extern "C" Item pn_io_websocket_open1(Item url) { return pn_io_websocket_open2(url, ItemNull); }

extern "C" Item pn_io_websocket_listen2(Item port_item, Item options) {
    int64_t port;
    if (!lambda_item_to_int64_exact(port_item, &port) || port < 0 || port > 65535)
        return io_fs_error(ERR_TYPE_MISMATCH, "io.websocket.listen: port must be an int from 0 to 65535");
    if (!network_sockets_ready()) return ws_error("sockets unavailable");
    WebSocket* s = ws_new();
    if (!s) return ItemError;
    IoFsOptionMap opts;
    Item error = ItemNull;
    int64_t max_bytes = (int64_t)s->max_bytes;
    if (!io_fs_options_open(options, "io.websocket.listen", SYSPROC_IO_WEBSOCKET_LISTEN, &opts, &error) ||
        !ws_message_limit(&opts, &max_bytes, &error) ||
        !io_fs_option_text_list(&opts, "origins", &s->origins, &error) ||
        !io_fs_option_text_list(&opts, "protocols", &s->protocols, &error)) { ws_free(s); return error; }
    if (!ws_protocols_valid(s->protocols)) { ws_free(s); return ws_error("invalid or duplicate subprotocol"); }
    s->max_bytes = (size_t)max_bytes;
    s->listener = true;
    s->socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)port);
    socklen_t size = sizeof(address);
    if (s->socket == NETWORK_INVALID_SOCKET || bind(s->socket, (struct sockaddr*)&address, sizeof(address)) != 0 ||
        listen(s->socket, 16) != 0 || getsockname(s->socket, (struct sockaddr*)&address, &size) != 0 ||
        !network_socket_nonblocking(s->socket)) { ws_free(s); return ws_error("cannot bind the loopback listener"); }
    s->port = ntohs(address.sin_port);
    s->waiter = lambda_socket_waiter_new((uint64_t)s->socket);
    if (!s->waiter) { ws_free(s); return ws_error("cannot watch the listener"); }
    log_debug("io.websocket: loopback listener on port %d", s->port);
    return ws_publish(s);
}
extern "C" Item pn_io_websocket_listen(Item port) { return pn_io_websocket_listen2(port, ItemNull); }

extern "C" Item pn_io_websocket_accept2(Item listener, Item timeout_item) {
    Item error = ItemNull;
    int64_t timeout;
    if (!ws_read_timeout(timeout_item, &timeout, &error)) return error;
    WebSocket* l = ws_begin(listener, false, true, &error);
    if (!l) return error;
    uint64_t deadline = ws_deadline(timeout);
    NetworkSocket fd = NETWORK_INVALID_SOCKET;
    while (true) {
        fd = accept(l->socket, NULL, NULL);
        if (fd != NETWORK_INVALID_SOCKET) break;
        if (!network_socket_would_block()) { error = ws_error("accept failed"); break; }
        if (!ws_wait(l, false, deadline, &error)) break;
    }
    if (fd == NETWORK_INVALID_SOCKET) { ws_end(l, false); return error; }
    // retain listener configuration while the accepted handshake can suspend.
    struct ListenerOwner { WebSocket* value; ~ListenerOwner() { ws_end(value, false); } } listener_owner = {l};
    WebSocket* s = ws_new();
    if (!s) { network_socket_close(fd); return ItemError; }
    // accepted connections retain the listener's admission bound, in both directions.
    s->max_bytes = l->max_bytes;
    s->socket = fd;
    network_socket_no_sigpipe(fd);
    s->waiter = network_socket_nonblocking(fd) ? lambda_socket_waiter_new((uint64_t)fd) : NULL;
    if (!s->waiter) { ws_free(s); return ws_error("cannot watch the accepted socket"); }
    RootFrame roots(1);
    Rooted<Item> handle(roots, ws_publish(s));
    if (get_type_id(handle.get()) == LMD_TYPE_ERROR) return handle.get();
    s->active++;
    StrBuf* head = ws_read_head(s, &error);
    char upgrade[64], connection[128], version[32], key[64], origin[1024], host[1024], expected[32], protocols[WS_HEADER_MAX];
    size_t decoded_size = 0;
    const char* line_end = head ? strstr(head->str, "\r\n") : NULL;
    const char* target_end = head ? strchr(head->str + 4, ' ') : NULL;
    int origin_field = head ? ws_header(head, "Origin", origin, sizeof(origin)) : -1;
    int protocol_field = head ? ws_header(head, "Sec-WebSocket-Protocol", protocols, sizeof(protocols), true) : -1;
    bool origin_ok = origin_field == 0 || (origin_field == 1 && ws_list_has(l->origins, origin));
    bool target_ok = target_end != NULL;
    for (const char* p = head ? head->str + 4 : NULL; target_ok && p < target_end; p++)
        if ((unsigned char)*p <= 32 || (unsigned char)*p == 127 || *p == '#') target_ok = false;
    bool ok = head && ws_headers_valid(head) && !strncmp(head->str, "GET /", 5) && target_end &&
        target_ok && line_end - target_end == 9 && !memcmp(target_end, " HTTP/1.1", 9) &&
        (ws_header(head, "Upgrade", upgrade, sizeof(upgrade), true) == 1) && ws_header_token(upgrade, "websocket") &&
        (ws_header(head, "Connection", connection, sizeof(connection), true) == 1) && ws_header_token(connection, "upgrade") &&
        (ws_header(head, "Host", host, sizeof(host)) == 1) && host[0] &&
        (ws_header(head, "Sec-WebSocket-Version", version, sizeof(version)) == 1) && !strcmp(version, "13") &&
        (ws_header(head, "Sec-WebSocket-Key", key, sizeof(key)) == 1) &&
        protocol_field >= 0 && (protocol_field == 0 || protocols[0]) && origin_ok;
    uint8_t* decoded = ok ? base64_decode(key, strlen(key), &decoded_size) : NULL;
    char canonical[25] = {};
    if (decoded && decoded_size == 16) base64_encode(decoded, decoded_size, canonical, BASE64_STD);
    ok = ok && decoded && decoded_size == 16 && !strcmp(key, canonical) && ws_accept_key(key, expected);
    if (ok && protocols[0]) {
        // validate all offers before choosing in server preference order.
        ArrayList* offered = arraylist_new(0);
        if (!offered) ok = false;
        char* p = protocols;
        while (*p && ok) {
            while (*p == ' ' || *p == '\t') p++;
            char* end = strchr(p, ',');
            bool last = !end;
            char* next = end ? end + 1 : p + strlen(p);
            if (!end) end = next;
            while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
            *end = 0;
            ok = (last || *next) && io_websocket_protocol_token(p) && !ws_list_has(offered, p) && arraylist_append(offered, p) != 0;
            p = next;
        }
        if (ok && l->protocols) for (int i = 0; i < l->protocols->length; i++) {
            const char* candidate = (const char*)l->protocols->data[i];
            if (ws_list_has(offered, candidate)) { s->protocol = mem_strdup(candidate, MEM_CAT_NETWORK); break; }
        }
        arraylist_free(offered);
    }
    mem_free(decoded);
    strbuf_free(head);
    if (ok) {
        StrBuf* reply = strbuf_new_cap(256);
        strbuf_append_format(reply, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n", expected);
        if (s->protocol) strbuf_append_format(reply, "Sec-WebSocket-Protocol: %s\r\n", s->protocol);
        strbuf_append_str(reply, "\r\n");
        ok = ws_write(s, reply->str, reply->length, ws_deadline(s->timeout_ms), &error);
        strbuf_free(reply);
    }
    if (!ok) {
        Item ignored = ItemNull;
        const char* refusal = origin_ok ? "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n" :
            "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        ws_write(s, refusal, strlen(refusal), ws_deadline(s->timeout_ms), &ignored);
        ws_invalidate(s);
        ws_end(s, false);
        return get_type_id(error) == LMD_TYPE_ERROR ? error : ws_error("invalid or unsupported WebSocket upgrade request");
    }
    ws_end(s, false);
    return handle.get();
}
extern "C" Item pn_io_websocket_accept1(Item listener) { return pn_io_websocket_accept2(listener, (Item){.item = i2it(0)}); }

extern "C" Item pn_io_websocket_send(Item handle, Item data) {
    Item error = ItemNull;
    TypeId type = get_type_id(data);
    bool binary = type == LMD_TYPE_BINARY;
    if (!binary && type != LMD_TYPE_NULL && !is_text_type_id(type))
        return io_fs_error(ERR_TYPE_MISMATCH, "io.websocket.send: data must be text or binary");
    WebSocket* s = ws_begin(handle, true, false, &error);
    if (!s) return error;
    if (s->closing) { ws_end(s, true); return io_fs_error(ERR_INVALID_STATE, "io.websocket: connection is closing"); }
    const char* bytes = binary ? (const char*)binary_data(data.get_binary()) : type == LMD_TYPE_NULL ? "" : data.get_chars();
    size_t length = binary ? binary_length(data.get_binary()) : type == LMD_TYPE_NULL ? 0 : data.get_len();
    bool ok = length <= s->max_bytes && (binary || utf8_valid(bytes, length));
    if (!ok) error = ws_error("outgoing message is too large or invalid UTF-8");
    else ok = ws_frame(s, binary ? 2 : 1, bytes, length, &error);
    while (ok && s->pong_pending) {
        // copy before parking: a later ping may replace the pending pong.
        StrBuf* pong = strbuf_dup(s->pong);
        s->pong_pending = false;
        ok = pong && ws_frame(s, 10, pong->str, pong->length, &error);
        strbuf_free(pong);
    }
    if (ok && s->closing && !s->close_sent) {
        s->close_sent = true;
        ok = ws_frame(s, 8, s->close_payload->str, s->close_payload->length, &error);
        if (s->close_received) ws_invalidate(s);
    }
    if (!ok) ws_invalidate(s); // a partial write must never be retried as a fresh frame
    ws_end(s, true);
    return ok ? ItemNull : error;
}

static Item ws_event(unsigned opcode, const char* bytes, size_t length, int code, bool clean = false) {
    static const char* names[] = {"type", "data", "code", "was_clean"};
    static const TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_ANY, LMD_TYPE_INT, LMD_TYPE_BOOL};
    RootFrame roots(3);
    const char* kind = opcode == 1 ? "text" : opcode == 2 ? "binary" : "close";
    Rooted<Item> type(roots, io_fs_make_string(kind, strlen(kind)));
    Rooted<Item> data(roots, ItemNull);
    if (opcode == 2) {
        Binary* binary = heap_binary_from_bytes(bytes, (int64_t)length);
        data.set(binary ? (Item){.item = x2it(binary)} : ItemError);
    } else data.set(io_fs_make_string(bytes, length));
    Rooted<Map*> result(roots, map_alloc_for_type(runtime_result_shape(names, types, 4), NULL, 0));
    if (!result.get()) return ItemError;
    Item values[] = {type.get(), data.get(), (Item){.item = i2it(code)}, (Item){.item = clean ? ITEM_TRUE : ITEM_FALSE}};
    map_fill_items(result.get(), values, 4);
    return (Item){.map = result.get()};
}
static bool ws_close_code(unsigned code) {
    return (code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006) || (code >= 3000 && code < 5000);
}

extern "C" Item pn_io_websocket_receive2(Item handle, Item timeout_item) {
    Item error = ItemNull;
    int64_t timeout;
    if (!ws_read_timeout(timeout_item, &timeout, &error)) return error;
    WebSocket* s = ws_begin(handle, false, false, &error);
    if (!s) return error;
    uint64_t deadline = ws_deadline(timeout);
    Item result = ItemNull;
    bool failed = false;
    bool retryable_timeout = true;
    while (true) {
        size_t available = s->input->length;
        const unsigned char* p = (const unsigned char*)s->input->str;
        if (available < 2) { if (!ws_read_more(s, deadline, s->max_bytes + 14, &error)) { failed = true; break; } continue; }
        bool final = (p[0] & 0x80) != 0, masked = (p[1] & 0x80) != 0;
        unsigned opcode = p[0] & 15;
        unsigned length_tag = p[1] & 127;
        size_t header = 2 + (length_tag == 126 ? 2 : length_tag == 127 ? 8 : 0) + (masked ? 4 : 0);
        if ((p[0] & 0x70) || masked == s->client || (opcode != 0 && opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 && opcode != 10)) {
            error = ws_error("invalid frame flags, opcode or masking direction"); failed = true; break;
        }
        if (available < header) { if (!ws_read_more(s, deadline, s->max_bytes + 14, &error)) { failed = true; break; } continue; }
        uint64_t length = length_tag;
        if (length_tag >= 126) {
            length = 0;
            size_t count = length_tag == 126 ? 2 : 8;
            for (size_t i = 0; i < count; i++) length = (length << 8) | p[2 + i];
            if ((length_tag == 126 && length < 126) || (length_tag == 127 && (length < 65536 || (p[2] & 0x80)))) {
                error = ws_error("noncanonical frame length"); failed = true; break;
            }
        }
        if (opcode >= 8 && (!final || length > 125)) {
            error = ws_error("invalid control frame"); failed = true; break;
        }
        if (opcode < 8 && (length > s->max_bytes || length > s->max_bytes - s->fragments->length)) {
            error = ws_error("message exceeds max_message_bytes"); failed = true; break;
        }
        if (available < header + (size_t)length) {
            if (!ws_read_more(s, deadline, header + (size_t)length, &error)) { failed = true; break; }
            continue;
        }
        char* payload = s->input->str + header;
        if (masked) for (size_t i = 0; i < (size_t)length; i++) payload[i] ^= p[header - 4 + i % 4];
        if (opcode >= 8) {
            if (opcode == 8) {
                unsigned code = length >= 2 ? ((unsigned char)payload[0] << 8) | (unsigned char)payload[1] : 1005;
                size_t skip = length >= 2 ? 2 : 0;
                if (length == 1 || (length >= 2 && !ws_close_code(code))) {
                    error = ws_error("invalid close status"); failed = true; break;
                }
                if (!utf8_valid(payload + skip, (size_t)length - skip)) {
                    error = ws_error("invalid close reason UTF-8"); failed = true; break;
                }
                s->closing = s->close_received = true;
                if (!s->close_sent) {
                    strbuf_reset(s->close_payload);
                    strbuf_append_str_n(s->close_payload, payload, (size_t)length);
                    if (!s->writing) {
                        s->writing = true;
                        s->close_sent = ws_frame(s, 8, payload, (size_t)length, &error);
                        s->writing = false;
                    }
                }
                // a pending application frame must finish before its close echo.
                if (s->writing) {
                    s->close_waiter = lambda_task_running();
                    Item wake = lambda_task_suspend(s->close_waiter);
                    s->close_waiter = NULL;
                    if (get_type_id(wake) == LMD_TYPE_ERROR) { error = wake; failed = true; break; }
                }
                result = ws_event(8, payload + skip, (size_t)length - skip, (int)code, s->close_sent);
                ws_invalidate(s);
                break;
            }
            if (opcode == 9) {
                // serialize control writes with an application writer to preserve framing.
                if (s->writing) {
                    strbuf_reset(s->pong);
                    strbuf_append_str_n(s->pong, payload, (size_t)length);
                    s->pong_pending = true;
                } else {
                    s->writing = true;
                    bool ok = ws_frame(s, 10, payload, (size_t)length, &error);
                    s->writing = false;
                    if (!ok) { failed = true; retryable_timeout = false; break; }
                }
            }
            ws_consume(s->input, header + (size_t)length);
            continue;
        }
        if ((opcode == 0 && !s->fragment_opcode) || (opcode != 0 && s->fragment_opcode)) {
            error = ws_error("invalid continuation sequence"); failed = true; break;
        }
        if (opcode != 0) s->fragment_opcode = opcode;
        if (!strbuf_ensure_cap(s->fragments, s->fragments->length + (size_t)length + 1)) { error = ItemError; failed = true; break; }
        strbuf_append_str_n(s->fragments, payload, (size_t)length);
        ws_consume(s->input, header + (size_t)length);
        if (!final) continue;
        if (s->fragment_opcode == 1 && !utf8_valid(s->fragments->str, s->fragments->length)) {
            error = ws_error("invalid text UTF-8"); failed = true; break;
        }
        result = ws_event(s->fragment_opcode, s->fragments->str, s->fragments->length, 0);
        s->fragment_opcode = 0;
        strbuf_reset(s->fragments);
        break;
    }
    if (failed) {
        // a timeout retains incomplete frames; cancellation/failure closes the stream.
        LambdaError* failure = get_type_id(error) == LMD_TYPE_ERROR ? it2err(error) : NULL;
        if (!failure || failure->code != ERR_NETWORK_TIMEOUT || !retryable_timeout) {
            if (!s->closed && !s->writing && !s->close_sent && failure && failure->code == ERR_NETWORK_ERROR) {
                unsigned code = strstr(failure->message, "UTF-8") ? 1007 : strstr(failure->message, "max_message_bytes") ? 1009 : 1002;
                char status[] = {(char)(code >> 8), (char)code};
                Item ignored = ItemNull;
                s->writing = true;
                ws_frame(s, 8, status, 2, &ignored);
                s->writing = false;
            }
            ws_invalidate(s);
        }
        result = error;
    }
    ws_end(s, false);
    return result;
}
extern "C" Item pn_io_websocket_receive1(Item handle) { return pn_io_websocket_receive2(handle, (Item){.item = i2it(0)}); }

Item io_websocket_protocol(Item handle) {
    WebSocket* s = ws_of(handle);
    const char* protocol = s && s->protocol ? s->protocol : "";
    return io_fs_make_string(protocol, strlen(protocol));
}

void io_websocket_abort(Item handle) { WebSocket* s = ws_of(handle); if (s) ws_invalidate(s); }

Item io_websocket_close_start(Item handle, Item code, Item reason) {
    WebSocket* s = ws_of(handle);
    if (!s) {
        if (get_type_id(handle) == LMD_TYPE_VMAP && handle.vmap && handle.vmap->host_type == &websocket_brand) return ItemNull;
        return io_fs_error(ERR_TYPE_MISMATCH, "io.websocket.close: expected a connection or listener");
    }
    Item error = ItemNull;
    if (s->listener) { ws_invalidate(s); return ItemNull; }
    if (s->closing) return ItemNull;
    int64_t status = 0;
    bool has_code = get_type_id(code) != LMD_TYPE_NULL;
    if ((has_code && (!lambda_item_to_int64_exact(code, &status) || !ws_close_code((unsigned)status))) ||
        (get_type_id(reason) != LMD_TYPE_NULL && !is_text_type_id(get_type_id(reason))))
        return io_fs_error(ERR_TYPE_MISMATCH, "io.websocket.close: invalid code or reason");
    const char* bytes = get_type_id(reason) == LMD_TYPE_NULL ? "" : reason.get_chars();
    size_t length = get_type_id(reason) == LMD_TYPE_NULL ? 0 : reason.get_len();
    if (length > 123 || !utf8_valid(bytes, length)) return ws_error("close reason exceeds 123 UTF-8 bytes or is invalid");
    if (!has_code && length) status = 1000;
    strbuf_reset(s->close_payload);
    if (has_code || length) {
        char status_bytes[] = {(char)(status >> 8), (char)status};
        strbuf_append_str_n(s->close_payload, status_bytes, 2);
        strbuf_append_str_n(s->close_payload, bytes, length);
    }
    s->closing = true;
    // bound the handshake even when another task is already receiving.
    s->close_timer = (uv_timer_t*)mem_calloc(1, sizeof(uv_timer_t), MEM_CAT_NETWORK);
    if (!s->close_timer || uv_timer_init(lambda_uv_loop(), s->close_timer) != 0) {
        mem_free(s->close_timer); s->close_timer = NULL; ws_invalidate(s); return ItemError;
    }
    s->close_timer->data = s;
    uv_timer_start(s->close_timer, [](uv_timer_t* timer) { ws_invalidate((WebSocket*)timer->data); }, WS_CLOSE_TIMEOUT_MS, 0);
    if (!s->writing) {
        s->active++;
        s->writing = true;
        s->close_sent = ws_frame(s, 8, s->close_payload->str, s->close_payload->length, &error);
        if (!s->close_sent) ws_invalidate(s);
        ws_end(s, true);
    }
    return error;
}

extern "C" Item pn_io_websocket_close3(Item handle, Item code, Item reason) {
    Item result = io_websocket_close_start(handle, code, reason);
    if (get_type_id(result) == LMD_TYPE_ERROR) return result;
    WebSocket* s = ws_of(handle);
    if (!s || s->listener || s->reading) return ItemNull;
    // the procedural close waits for the peer; browser close queues this work.
    RootFrame roots(1);
    Rooted<Item> event(roots, ItemNull);
    while ((s = ws_of(handle)) && !s->closed) {
        event.set(pn_io_websocket_receive2(handle, (Item){.item = i2it(WS_CLOSE_TIMEOUT_MS)}));
        if (get_type_id(event.get()) == LMD_TYPE_ERROR) {
            io_websocket_abort(handle);
            return event.get();
        }
    }
    return ItemNull;
}
extern "C" Item pn_io_websocket_close2(Item handle, Item code) { return pn_io_websocket_close3(handle, code, ItemNull); }
extern "C" Item pn_io_websocket_close(Item handle) { return pn_io_websocket_close3(handle, (Item){.item = i2it(1000)}, ItemNull); }
