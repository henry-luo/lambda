#pragma once

// js_node_uv.hpp — shared libuv plumbing for the Node-compatibility modules.
//
// Every module used to carry its own uv-error factory (net's make_uv_error,
// dns's make_dns_error_with_syscall, http's http_error_from_uv,
// child_process's make_uv_spawn_error, fs's make_fs_error). They agreed on
// the field set and disagreed only on the message text, so the stamping lives
// here once and each caller supplies the message it must produce.

#include "js_runtime.h"
#include "../lambda-data.hpp"
#include "../../lib/uv_loop.h"

struct JsNodeUvError {
    int status = 0;             // negative uv error code
    const char* syscall = NULL; // stamped as err.syscall when set
    const char* message = NULL; // NULL -> uv_strerror(status)
    const char* code = NULL;    // NULL -> uv_err_name(status)
    const char* path = NULL;    // stamped as err.path
    const char* address = NULL; // stamped as err.address
    const char* hostname = NULL;// stamped as err.hostname
    int port = -1;              // stamped as err.port when >= 0
    Item extra_key = ItemNull;  // one optional extra property (e.g. spawnargs)
    Item extra_value = ItemNull;
};

// Build a Node-shaped system error. Always stamps code and errno; stamps each
// remaining field only when the caller supplied it.
Item js_node_uv_error(const JsNodeUvError& spec);

// uv_err_name with Node's getaddrinfo remap: a name lookup that finds nothing
// reports ENOTFOUND, not EAI_NONAME.
const char* js_node_uv_code(int status, const char* syscall);

// ---------------------------------------------------------------------------
// Stream writes
//
// Every Node module submits writes the same way: allocate a request wrapper,
// copy the bytes, hand them to libuv, and free both when the write settles.
// The ownership rule is the part that kept going wrong (see C0.4): libuv only
// invokes the callback for a request it accepted, so a rejected submission
// leaves the caller as sole owner. js_node_stream_write makes that impossible
// to get wrong — `done` runs exactly once either way, and the helper owns the
// wrapper and the byte copy on both paths.
// ---------------------------------------------------------------------------

// Called once per submitted write. `status` is 0 on success or a negative uv
// error. `data`/`len` are the copied bytes, still valid for the duration of
// the call; the helper frees them afterwards.
typedef void (*JsNodeWriteDone)(void* ud, int status, const char* data, size_t len);

// Returns 0 when libuv accepted the request, or the negative uv error. `done`
// has already run by the time a non-zero value is returned.
int js_node_stream_write(uv_stream_t* stream, const char* data, size_t len,
                         JsNodeWriteDone done, void* ud);

// As above, but adopts an already-allocated buffer instead of copying it, so
// callers that have staged the bytes themselves do not pay a second copy.
int js_node_stream_write_owned(uv_stream_t* stream, char* data, size_t len,
                               JsNodeWriteDone done, void* ud);
