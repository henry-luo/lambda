#pragma once

// js_node_common.hpp — small helpers shared by the Node-compatibility modules
// (net, tls, http, https, dns, stream, child_process, process). Each of these
// existed as a per-file static clone; they live here so the modules share one
// definition instead of drifting apart.

#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "js_typed_array.h"
#include "../../lib/mem.h"
#include "../../lib/uv_loop.h"

// Native subsystems park their C-side handle on the JS object as an integer
// under a private key. The receiver may be a plain map or a VMap projection,
// and an object that never got a handle reads back as absent, not as zero.
static inline void* js_node_handle_from_object(Item self, const char* key) {
    TypeId type = get_type_id(self);
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_VMAP) return NULL;
    Item handle_item = js_get_key_cstr(self, key);
    if (get_type_id(handle_item) != LMD_TYPE_INT) return NULL;
    return (void*)(uintptr_t)it2i(handle_item);
}

// `on`/`once`/`removeListener` are the same three steps on every Node emitter
// facade: take the receiver, ignore a non-string event name, delegate to the
// shared emitter, and return the receiver so calls chain.
#define JS_DEFINE_EMITTER_FACADE(fn_name, delegate) \
    extern "C" Item fn_name(Item event_item, Item callback) { \
        Item self = js_get_this(); \
        if (get_type_id(event_item) != LMD_TYPE_STRING) return self; \
        delegate(self, event_item, callback); \
        return self; \
    }

// Node's callback-taking entry points all reject a non-callable completion
// callback the same way, before doing any work.
#define JS_REQUIRE_CALLBACK(callback) \
    if (!js_is_callable(callback)) { \
        return js_throw_invalid_arg_type("callback", "function", callback); \
    }

// libuv read-buffer allocator. Every Node module reads into a freshly
// allocated buffer that its read callback frees, so they all want this.
static inline void js_node_alloc_cb(uv_handle_t* handle, size_t suggested_size,
        uv_buf_t* buf) {
    buf->base = (char*)mem_alloc(suggested_size, MEM_CAT_JS_RUNTIME);
    buf->len = buf->base ? suggested_size : 0;
}

// An options bag written as a JS object literal reaches native code as a MAP,
// OBJECT or VMAP depending on how it was built.
static inline bool js_node_is_plain_object(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_VMAP;
}

// As above, plus ELEMENT: modules that also accept DOM-ish carriers.
static inline bool js_node_is_object_like(Item item) {
    return js_node_is_plain_object(item) || get_type_id(item) == LMD_TYPE_ELEMENT;
}

// Anything with properties, including arrays and callables — used where the
// argument may legitimately be a list or a function (dns, stream).
static inline bool js_node_is_property_carrier(Item item) {
    TypeId type = get_type_id(item);
    return type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY ||
           type == LMD_TYPE_ELEMENT || type == LMD_TYPE_FUNC;
}

// size_t out-param overload of js_item_bytes for the transport paths that
// measure lengths in size_t. The byte extraction itself lives in
// js_typed_array.cpp and handles string, TypedArray, ArrayBuffer and DataView.
static inline bool js_item_bytes(Item item, const char** data, size_t* len) {
    int measured = 0;
    if (!js_item_bytes(item, data, &measured)) return false;
    *len = measured > 0 ? (size_t)measured : 0;
    return true;
}
