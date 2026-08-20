#pragma once

#include "../../js/js_runtime_state.hpp"
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define JS_PERMISSION_MAX_GRANTS 128
#define JS_CRYPTO_MAX_LIVE_CONTEXTS 4096
#define JS_CJS_STACK_MAX 128
#define JS_DIAGNOSTICS_CHANNEL_MAX 512
#define JS_DIAGNOSTICS_DEFERRED_ERROR_MAX 64

// CommonJS metadata is semantic Node module state. A nested require observes
// its own parent stack, but unrelated JS realms must never share the stack.
struct JsCjsState {
    Item module_stack_slots[JS_CJS_STACK_MAX] = {};
    JsItemStack module_stack = {};
};

struct JsDiagnosticsChannelState : JsRootedState {
    Item namespace_object = {};
    Item channel_names[JS_DIAGNOSTICS_CHANNEL_MAX] = {};
    Item channels[JS_DIAGNOSTICS_CHANNEL_MAX] = {};
    Item channel_prototype = {};
    Item tracing_channel_prototype = {};
    Item bounded_channel_prototype = {};
    Item channel_constructor = {};
    Item tracing_channel_constructor = {};
    Item bounded_channel_constructor = {};
    Item deferred_errors[JS_DIAGNOSTICS_DEFERRED_ERROR_MAX] = {};
    uint64_t namespace_epoch = UINT64_MAX;
    int channel_count = 0;
    int deferred_error_count = 0;
};

struct JsCommonJsCompileCacheState {
    char directory[4096] = {};
    bool enabled = false;
    bool disabled = false;
    bool reported = false;
};

typedef struct JsPermissionGrant {
    char path[PATH_MAX];
    bool wildcard_all;
    bool wildcard_prefix;
    bool directory;
    bool active;
} JsPermissionGrant;

// Permission flags and grant tables are Node launch-policy state. Keeping the
// policy in NodeRuntimeSession avoids embedding two PATH_MAX-sized slabs in
// every JavaScript realm.
struct JsPermissionPolicy {
    bool initialized = false;
    bool enabled = false;
    bool child_process = false;
    bool net = false;
    bool inspector = false;
    bool addon = false;
    bool wasi = false;
    JsPermissionGrant fs_read_grants[JS_PERMISSION_MAX_GRANTS] = {};
    JsPermissionGrant fs_write_grants[JS_PERMISSION_MAX_GRANTS] = {};
};

// Native crypto handles are Node module resources, not JavaScript realm
// metadata. The fixed live-context registries are paid only by Node sessions.
struct JsCryptoNativeState {
    bool pseudo_random_warning_emitted;
    void* hmac_contexts[JS_CRYPTO_MAX_LIVE_CONTEXTS];
    int hmac_context_count;
    void* hash_contexts[JS_CRYPTO_MAX_LIVE_CONTEXTS];
    int hash_context_count;
    void* sign_verify_contexts[JS_CRYPTO_MAX_LIVE_CONTEXTS];
    int sign_verify_context_count;
    void* cipher_contexts[JS_CRYPTO_MAX_LIVE_CONTEXTS];
    int cipher_context_count;
};
