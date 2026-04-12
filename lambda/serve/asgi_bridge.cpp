/**
 * @file asgi_bridge.cpp
 * @brief ASGI bridge implementation — JSON IPC over pipes or Unix domain sockets
 */

#include "asgi_bridge.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include "../../lib/mem.h"
#include <cstdio>

// ============================================================================
// Base64 Encoding/Decoding
// ============================================================================

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* base64_encode(const char *data, size_t len) {
    size_t out_len = 4 * ((len + 2) / 3) + 1;
    char *out = (char*)mem_alloc(out_len, MEM_CAT_SERVE);
    size_t i = 0, j = 0;

    while (i < len) {
        uint32_t a = (i < len) ? (unsigned char)data[i++] : 0;
        uint32_t b = (i < len) ? (unsigned char)data[i++] : 0;
        uint32_t c = (i < len) ? (unsigned char)data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : b64_table[triple & 0x3F];
    }
    out[j] = '\0';
    return out;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static char* base64_decode(const char *data, size_t *out_len) {
    size_t len = strlen(data);
    size_t alloc = 3 * len / 4 + 1;
    char *out = (char*)mem_alloc(alloc, MEM_CAT_SERVE);
    size_t i = 0, j = 0;

    while (i < len) {
        int a = (i < len) ? b64_decode_char(data[i++]) : 0;
        int b = (i < len) ? b64_decode_char(data[i++]) : 0;
        int c = (i < len) ? b64_decode_char(data[i++]) : 0;
        int d = (i < len) ? b64_decode_char(data[i++]) : 0;

        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (c < 0) c = 0;
        if (d < 0) d = 0;

        uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;
        out[j++] = (triple >> 16) & 0xFF;
        if (data[i-2] != '=') out[j++] = (triple >> 8) & 0xFF;
        if (data[i-1] != '=') out[j++] = triple & 0xFF;
    }
    out[j] = '\0';
    if (out_len) *out_len = j;
    return out;
}

// ============================================================================
// JSON Message Building
// ============================================================================

static char* build_request_message(uint64_t id, HttpRequest *req) {
    StrBuf *buf = strbuf_new_cap(1024);

    strbuf_append_str(buf, "{\"type\":\"http\",\"id\":");
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%llu", (unsigned long long)id);
    strbuf_append_str(buf, id_str);

    // method
    const char *method_str = "GET";
    switch (req->method) {
        case HTTP_GET:    method_str = "GET";    break;
        case HTTP_POST:   method_str = "POST";   break;
        case HTTP_PUT:    method_str = "PUT";     break;
        case HTTP_DELETE: method_str = "DELETE";  break;
        case HTTP_PATCH:  method_str = "PATCH";   break;
        case HTTP_HEAD:   method_str = "HEAD";    break;
        default:          method_str = "GET";     break;
    }
    strbuf_append_str(buf, ",\"method\":\"");
    strbuf_append_str(buf, method_str);
    strbuf_append_char(buf, '"');

    // path
    strbuf_append_str(buf, ",\"path\":\"");
    if (req->path) strbuf_append_str(buf, req->path);
    strbuf_append_char(buf, '"');

    // query_string
    strbuf_append_str(buf, ",\"query_string\":\"");
    if (req->query_string) strbuf_append_str(buf, req->query_string);
    strbuf_append_char(buf, '"');

    // headers as array of [name, value] pairs
    strbuf_append_str(buf, ",\"headers\":[");
    HttpHeader *h = req->headers;
    int first = 1;
    while (h) {
        if (!first) strbuf_append_char(buf, ',');
        first = 0;
        strbuf_append_str(buf, "[\"");
        if (h->name) strbuf_append_str(buf, h->name);
        strbuf_append_str(buf, "\",\"");
        if (h->value) strbuf_append_str(buf, h->value);
        strbuf_append_str(buf, "\"]");
        h = h->next;
    }
    strbuf_append_char(buf, ']');

    // body (base64-encoded)
    strbuf_append_str(buf, ",\"body\":\"");
    if (req->body && req->body_len > 0) {
        char *b64 = base64_encode(req->body, req->body_len);
        strbuf_append_str(buf, b64);
        mem_free(b64);
    }
    strbuf_append_char(buf, '"');

    strbuf_append_str(buf, "}\n");

    size_t len = buf->length;
    char *result = (char*)mem_alloc(len + 1, MEM_CAT_SERVE);
    memcpy(result, buf->str, len + 1);
    strbuf_free(buf);

    return result;
}

// ============================================================================
// Response Parsing
// ============================================================================

// simple JSON field extraction for response parsing
static const char* json_find_field(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int parse_response_message(const char *json, HttpResponse *resp) {
    // status
    const char *status_p = json_find_field(json, "status");
    if (status_p) {
        int status = (int)strtol(status_p, NULL, 10);
        http_response_status(resp, status);
    }

    // headers
    const char *headers_p = json_find_field(json, "headers");
    if (headers_p && *headers_p == '[') {
        const char *p = headers_p + 1;
        while (*p && *p != ']') {
            while (*p == ' ' || *p == '\n' || *p == ',') p++;
            if (*p != '[') break;
            p++; // skip [

            // parse name
            while (*p == ' ') p++;
            if (*p != '"') break;
            p++;
            const char *name_start = p;
            while (*p && *p != '"') p++;
            size_t name_len = p - name_start;
            if (*p == '"') p++;

            while (*p == ' ' || *p == ',') p++;

            // parse value
            if (*p != '"') break;
            p++;
            const char *val_start = p;
            while (*p && *p != '"') { if (*p == '\\') p++; p++; }
            size_t val_len = p - val_start;
            if (*p == '"') p++;

            while (*p == ' ') p++;
            if (*p == ']') p++;

            // set header
            char name_buf[256], val_buf[4096];
            if (name_len < sizeof(name_buf) && val_len < sizeof(val_buf)) {
                memcpy(name_buf, name_start, name_len);
                name_buf[name_len] = '\0';
                memcpy(val_buf, val_start, val_len);
                val_buf[val_len] = '\0';
                http_response_set_header(resp, name_buf, val_buf);
            }
        }
    }

    // body (base64-encoded)
    const char *body_p = json_find_field(json, "body");
    if (body_p && *body_p == '"') {
        body_p++;
        const char *body_end = body_p;
        while (*body_end && *body_end != '"') { if (*body_end == '\\') body_end++; body_end++; }

        size_t b64_len = body_end - body_p;
        char *b64 = (char*)mem_alloc(b64_len + 1, MEM_CAT_SERVE);
        memcpy(b64, body_p, b64_len);
        b64[b64_len] = '\0';

        size_t decoded_len = 0;
        char *decoded = base64_decode(b64, &decoded_len);
        mem_free(b64);

        if (decoded && decoded_len > 0) {
            http_response_write(resp, decoded, decoded_len);
        }
        mem_free(decoded);
    }

    return 0;
}

// ============================================================================
// Worker Lifecycle
// ============================================================================

static void on_worker_exit(uv_process_t *process, int64_t exit_status, int term_signal) {
    AsgiWorker *worker = (AsgiWorker*)process->data;
    worker->alive = 0;
    log_info("asgi: worker exited (status=%lld, signal=%d)",
             (long long)exit_status, term_signal);
}

static void on_worker_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    AsgiWorker *worker = (AsgiWorker*)stream->data;

    if (nread <= 0) {
        if (buf->base) mem_free(buf->base);
        if (nread < 0) {
            worker->alive = 0;
        }
        return;
    }

    // accumulate into read buffer
    if (worker->read_buf_len + nread >= worker->read_buf_cap) {
        worker->read_buf_cap = (worker->read_buf_len + nread) * 2;
        worker->read_buf = (char*)mem_realloc(worker->read_buf, worker->read_buf_cap, MEM_CAT_SERVE);
    }
    memcpy(worker->read_buf + worker->read_buf_len, buf->base, nread);
    worker->read_buf_len += nread;
    worker->read_buf[worker->read_buf_len] = '\0';

    mem_free(buf->base);

    // check for complete JSON message (newline-delimited)
    char *newline = strchr(worker->read_buf, '\n');
    if (newline) {
        *newline = '\0';
        // worker->read_buf now contains the complete JSON response
        // response will be consumed by the dispatch mechanism
        worker->busy = 0;

        // shift remaining data
        size_t consumed = newline - worker->read_buf + 1;
        size_t remaining = worker->read_buf_len - consumed;
        if (remaining > 0) {
            memmove(worker->read_buf, newline + 1, remaining);
        }
        worker->read_buf_len = remaining;
    }
}

static void alloc_buffer(uv_handle_t *handle, size_t suggested, uv_buf_t *buf) {
    buf->base = (char*)mem_alloc(suggested, MEM_CAT_SERVE);
    buf->len = suggested;
}

static int start_worker(AsgiBridge *bridge, int index) {
    AsgiWorker *worker = (AsgiWorker*)mem_calloc(1, sizeof(AsgiWorker), MEM_CAT_SERVE);
    worker->read_buf_cap = 4096;
    worker->read_buf = (char*)mem_alloc(worker->read_buf_cap, MEM_CAT_SERVE);
    worker->read_buf_len = 0;

    uv_pipe_init(bridge->loop, &worker->stdin_pipe, 0);
    uv_pipe_init(bridge->loop, &worker->stdout_pipe, 0);
    worker->stdout_pipe.data = worker;

    uv_stdio_container_t stdio[3];
    stdio[0].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_READABLE_PIPE);
    stdio[0].data.stream = (uv_stream_t*)&worker->stdin_pipe;
    stdio[1].flags = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    stdio[1].data.stream = (uv_stream_t*)&worker->stdout_pipe;
    stdio[2].flags = UV_INHERIT_FD;
    stdio[2].data.fd = 2;  // inherit stderr

    // build args: python3 asgi_bridge.py --app module:app
    const char *python = bridge->python_path ? bridge->python_path : "python3";
    char *args[8];
    int arg_count = 0;
    args[arg_count++] = (char*)python;
    args[arg_count++] = (char*)bridge->bridge_script;
    args[arg_count++] = (char*)"--app";
    args[arg_count++] = (char*)bridge->python_app;

    if (bridge->transport == ASGI_TRANSPORT_UDS && bridge->uds_path) {
        char uds_worker_path[256];
        snprintf(uds_worker_path, sizeof(uds_worker_path), "%s-%d.sock",
                 bridge->uds_path, index);
        args[arg_count++] = (char*)"--uds";
        args[arg_count++] = uds_worker_path;
    }
    args[arg_count] = NULL;

    uv_process_options_t options = {};
    options.file = python;
    options.args = args;
    options.stdio_count = 3;
    options.stdio = stdio;
    options.exit_cb = on_worker_exit;
    options.flags = UV_PROCESS_DETACHED;

    worker->process.data = worker;

    int r = uv_spawn(bridge->loop, &worker->process, &options);
    if (r != 0) {
        log_error("asgi: failed to spawn worker %d: %s", index, uv_strerror(r));
        mem_free(worker->read_buf);
        mem_free(worker);
        return -1;
    }

    worker->alive = 1;
    uv_read_start((uv_stream_t*)&worker->stdout_pipe, alloc_buffer, on_worker_read);
    uv_unref((uv_handle_t*)&worker->process);

    bridge->workers[index] = worker;
    log_info("asgi: started worker %d (pid=%d)", index, worker->process.pid);
    return 0;
}

// ============================================================================
// Bridge Lifecycle
// ============================================================================

AsgiBridge* asgi_bridge_create(Server *server, const char *python_app,
                                int worker_count, AsgiTransport transport) {
    if (!server || !python_app) return NULL;
    if (worker_count <= 0) worker_count = 4;

    AsgiBridge *bridge = (AsgiBridge*)mem_calloc(1, sizeof(AsgiBridge), MEM_CAT_SERVE);
    bridge->server = server;
    bridge->python_app = python_app;
    bridge->transport = transport;
    bridge->worker_count = worker_count;
    bridge->workers = (AsgiWorker**)mem_calloc(worker_count, sizeof(AsgiWorker*), MEM_CAT_SERVE);
    bridge->next_request_id = 1;
    bridge->loop = server->loop;

    // default bridge script path (same directory as lambda.exe)
    bridge->bridge_script = "lambda/serve/asgi_bridge.py";

    return bridge;
}

int asgi_bridge_start(AsgiBridge *bridge) {
    if (!bridge) return -1;

    int started = 0;
    for (int i = 0; i < bridge->worker_count; i++) {
        if (start_worker(bridge, i) == 0) started++;
    }

    if (started == 0) {
        log_error("asgi: no workers started");
        return -1;
    }

    log_info("asgi: %d/%d workers started", started, bridge->worker_count);
    return 0;
}

// ============================================================================
// Request Dispatch
// ============================================================================

static AsgiWorker* find_available_worker(AsgiBridge *bridge) {
    for (int i = 0; i < bridge->worker_count; i++) {
        AsgiWorker *w = bridge->workers[i];
        if (w && w->alive && !w->busy) return w;
    }
    return NULL;
}

int asgi_bridge_dispatch(AsgiBridge *bridge, HttpRequest *req, HttpResponse *resp) {
    if (!bridge || !req || !resp) return -1;

    AsgiWorker *worker = find_available_worker(bridge);
    if (!worker) {
        http_response_status(resp, 503);
        http_response_set_header(resp, "Content-Type", "application/json");
        http_response_write_str(resp, "{\"error\":\"no available ASGI workers\"}");
        return -1;
    }

    uint64_t id = bridge->next_request_id++;
    worker->busy = 1;
    worker->request_id = id;

    // build JSON request message
    char *msg = build_request_message(id, req);
    size_t msg_len = strlen(msg);

    // write to worker stdin
    uv_buf_t write_buf = uv_buf_init(msg, (unsigned int)msg_len);
    uv_write_t *write_req = (uv_write_t*)mem_calloc(1, sizeof(uv_write_t), MEM_CAT_SERVE);
    write_req->data = msg;

    int r = uv_write(write_req, (uv_stream_t*)&worker->stdin_pipe, &write_buf, 1,
        [](uv_write_t *wr, int status) {
            mem_free(wr->data);
            mem_free(wr);
        });

    if (r != 0) {
        log_error("asgi: failed to write to worker: %s", uv_strerror(r));
        mem_free(msg);
        mem_free(write_req);
        worker->busy = 0;
        http_response_status(resp, 502);
        http_response_write_str(resp, "{\"error\":\"failed to send to ASGI worker\"}");
        return -1;
    }

    // note: in a production implementation, we would use async completion
    // to parse the response from the worker's read buffer and send back
    // to the client. For now, this is a synchronous placeholder.
    // The actual response handling happens via on_worker_read callback
    // and would need request/response correlation via the id field.

    return 0;
}

// ============================================================================
// Catch-all Handler
// ============================================================================

static void asgi_catch_all_handler(HttpRequest *req, HttpResponse *resp, void *user_data) {
    AsgiBridge *bridge = (AsgiBridge*)user_data;
    asgi_bridge_dispatch(bridge, req, resp);
}

int asgi_bridge_mount(AsgiBridge *bridge, const char *prefix) {
    if (!bridge || !bridge->server || !prefix) return -1;

    char pattern[256];
    if (strcmp(prefix, "/") == 0) {
        snprintf(pattern, sizeof(pattern), "/*");
    } else {
        snprintf(pattern, sizeof(pattern), "%s/*", prefix);
    }

    server_all(bridge->server, pattern, asgi_catch_all_handler, bridge);
    log_info("asgi: mounted at %s", pattern);
    return 0;
}

// ============================================================================
// Cleanup
// ============================================================================

static void close_handle_cb(uv_handle_t *handle) {
    // no-op: handles freed with worker
}

void asgi_bridge_destroy(AsgiBridge *bridge) {
    if (!bridge) return;

    for (int i = 0; i < bridge->worker_count; i++) {
        AsgiWorker *w = bridge->workers[i];
        if (!w) continue;

        if (w->alive) {
            uv_process_kill(&w->process, SIGTERM);
        }

        if (!uv_is_closing((uv_handle_t*)&w->stdin_pipe))
            uv_close((uv_handle_t*)&w->stdin_pipe, close_handle_cb);
        if (!uv_is_closing((uv_handle_t*)&w->stdout_pipe))
            uv_close((uv_handle_t*)&w->stdout_pipe, close_handle_cb);
        if (!uv_is_closing((uv_handle_t*)&w->process))
            uv_close((uv_handle_t*)&w->process, close_handle_cb);

        mem_free(w->read_buf);
        mem_free(w);
    }

    mem_free(bridge->workers);
    mem_free(bridge);

    log_info("asgi: bridge destroyed");
}
