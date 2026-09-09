#pragma once

#include "../../js/js_runtime_state.hpp"
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// CommonJS metadata is semantic Node module state. A nested require observes
// its own parent stack, but unrelated JS realms must never share the stack.
struct JsCjsState {
    JsItemStack module_stack = {};
};

struct JsDiagnosticsChannelState : JsRootedState {
    Item namespace_object = {};
    // Each entry is a GC-owned [name, channel] pair. Keeping the pair in one
    // record prevents independently sized name/channel root arrays.
    Item channel_entries = {};
    Item channel_prototype = {};
    Item tracing_channel_prototype = {};
    Item bounded_channel_prototype = {};
    Item channel_constructor = {};
    Item tracing_channel_constructor = {};
    Item bounded_channel_constructor = {};
    Item deferred_errors = {};
    uint64_t namespace_epoch = UINT64_MAX;
};

struct JsCommonJsCompileCacheState {
    char* directory = NULL;
    bool enabled = false;
    bool disabled = false;
    bool reported = false;
};

enum JsPermissionGrantFlags : uint8_t {
    JS_PERMISSION_GRANT_READ = 1 << 0,
    JS_PERMISSION_GRANT_WRITE = 1 << 1,
    JS_PERMISSION_GRANT_WILDCARD_ALL = 1 << 2,
    JS_PERMISSION_GRANT_WILDCARD_PREFIX = 1 << 3,
    JS_PERMISSION_GRANT_DIRECTORY = 1 << 4,
};

typedef struct JsPermissionGrant {
    char* path;
    uint8_t flags;
} JsPermissionGrant;

// Permission flags and grants are Node launch-policy state. One dynamic row
// may grant read and write access, so policy does not duplicate each path.
struct JsPermissionPolicy {
    bool initialized = false;
    bool enabled = false;
    bool child_process = false;
    bool net = false;
    bool inspector = false;
    bool addon = false;
    bool wasi = false;
    ArrayList* grants = NULL;
};

// Native crypto handles are Node module resources, not JavaScript realm
// metadata. The fixed live-context registries are paid only by Node sessions.
// JSCU24: live native contexts are owned by the session's generation-checked
// resource table and named by rid, so no fixed handle tables live here.
struct JsCryptoNativeState {
    bool pseudo_random_warning_emitted;
};
