// js_node_uv.cpp — shared libuv plumbing for the Node-compatibility modules.

#include "js_node_uv.hpp"
#include "../../lib/mem.h"
#include <string.h>

const char* js_node_uv_code(int status, const char* syscall) {
    const char* code = uv_err_name(status);
    if (!code) return "UNKNOWN";
    // Node reports a name lookup that resolved to nothing as ENOTFOUND; uv
    // spells the same condition EAI_NONAME.
    if (syscall && strcmp(syscall, "getaddrinfo") == 0 &&
            strcmp(code, "EAI_NONAME") == 0) {
        return "ENOTFOUND";
    }
    return code;
}

Item js_node_uv_error(const JsNodeUvError& spec) {
    const char* code = spec.code ? spec.code : js_node_uv_code(spec.status, spec.syscall);
    const char* message = spec.message;
    if (!message) {
        message = uv_strerror(spec.status);
        if (!message) message = "unknown error";
    }
    RootFrame roots(1);
    Rooted<Item> err_root(roots, js_new_error(make_string_item(message)));
    js_set_key_cstr(err_root.get(), "code", make_string_item(code));
    js_set_key_cstr(err_root.get(), "errno", (Item){.item = i2it(spec.status)});
    if (spec.syscall) {
        js_set_key_cstr(err_root.get(), "syscall", make_string_item(spec.syscall));
    }
    if (spec.path) js_set_key_cstr(err_root.get(), "path", make_string_item(spec.path));
    if (spec.address) {
        js_set_key_cstr(err_root.get(), "address", make_string_item(spec.address));
    }
    if (spec.hostname) {
        js_set_key_cstr(err_root.get(), "hostname", make_string_item(spec.hostname));
    }
    if (spec.port >= 0) {
        js_set_key_cstr(err_root.get(), "port", (Item){.item = i2it(spec.port)});
    }
    if (spec.extra_key.item != ItemNull.item) {
        js_set_key_default(err_root.get(), spec.extra_key, spec.extra_value);
    }
    return err_root.get();
}

// ---------------------------------------------------------------------------
// Stream writes
// ---------------------------------------------------------------------------

typedef struct JsNodeWriteReq {
    uv_write_t req;
    char* data;
    size_t len;
    JsNodeWriteDone done;
    void* ud;
} JsNodeWriteReq;

static void js_node_write_settle(JsNodeWriteReq* wr, int status) {
    if (!wr) return;
    if (wr->done) wr->done(wr->ud, status, wr->data, wr->len);
    if (wr->data) mem_free(wr->data);
    mem_free(wr);
}

static void js_node_write_cb(uv_write_t* req, int status) {
    js_node_write_settle((JsNodeWriteReq*)req->data, status);
}

int js_node_stream_write_owned(uv_stream_t* stream, char* data, size_t len,
                               JsNodeWriteDone done, void* ud) {
    JsNodeWriteReq* wr =
        (JsNodeWriteReq*)mem_calloc(1, sizeof(JsNodeWriteReq), MEM_CAT_JS_RUNTIME);
    if (!wr) { if (data) mem_free(data); return UV_ENOMEM; }
    wr->data = data;
    wr->len = len;
    wr->done = done;
    wr->ud = ud;
    wr->req.data = wr;
    uv_buf_t buf = uv_buf_init(wr->data ? wr->data : (char*)"", (unsigned int)len);
    int status = uv_write(&wr->req, stream, &buf, 1, js_node_write_cb);
    // A rejected submission never reaches js_node_write_cb, so settle here —
    // once, and with this side as the only owner.
    if (status != 0) js_node_write_settle(wr, status);
    return status;
}

int js_node_stream_write(uv_stream_t* stream, const char* data, size_t len,
                         JsNodeWriteDone done, void* ud) {
    char* copy = NULL;
    if (len > 0) {
        copy = (char*)mem_alloc(len, MEM_CAT_JS_RUNTIME);
        if (!copy) return UV_ENOMEM;
        memcpy(copy, data, len);
    }
    return js_node_stream_write_owned(stream, copy, len, done, ud);
}
