#include "../../jube/jube_registry.h"
#include "node_path.hpp"
#include "node_constants.hpp"
#include "node_events.hpp"
#include "node_os.hpp"
#include "node_perf_hooks.hpp"
#include "node_process.hpp"
#include "node_querystring.hpp"
#include "node_punycode.hpp"
#include "node_string_decoder.hpp"
#include "node_timers.hpp"
#include "node_tty.hpp"
#include "node_url.hpp"
#include "node_v8.hpp"
#include "node_workers.hpp"

#include <cstring>

static const JubeHostAPI* node_core_host = NULL;
static void* node_core_session = NULL;

static Item node_core_path_namespace(void) {
    return node_path_namespace();
}

static Item node_core_path_win32_namespace(void) {
    return node_path_win32_namespace();
}

static Item node_core_string_decoder_namespace(void) {
    return node_string_decoder_namespace();
}

static Item node_core_querystring_namespace(void) {
    return node_querystring_namespace();
}

static Item node_core_os_namespace(void) {
    return node_os_namespace();
}

static Item node_core_url_namespace(void) {
    return node_url_namespace();
}

static Item node_core_events_namespace(void) {
    return node_events_namespace();
}

static Item node_core_punycode_namespace(void) {
    return node_punycode_namespace();
}

static Item node_core_timers_promises_namespace(void) {
    return node_timers_promises_namespace();
}

static Item node_core_timers_namespace(void) {
    return node_timers_namespace();
}

static Item node_core_constants_namespace(void) {
    return node_constants_namespace();
}

static Item node_core_v8_namespace(void) { return node_v8_namespace(); }
static Item node_core_perf_hooks_namespace(void) { return node_perf_hooks_namespace(); }
static Item node_core_workers_namespace(void) { return node_workers_namespace(); }
static Item node_core_tty_namespace(void) { return node_tty_namespace(); }

static Item node_core_host_namespace(const char* specifier) {
    if (!node_core_host || !node_core_session || !node_core_host->node ||
            !node_core_host->node->runtime ||
            !node_core_host->node->runtime->resolve_host_namespace || !specifier) {
        return ItemNull;
    }
    Item module_namespace = ItemNull;
    if (node_core_host->node->runtime->resolve_host_namespace(node_core_session, specifier,
            &module_namespace) != 0) {
        return ItemNull;
    }
    return module_namespace;
}

static Item node_core_module_namespace(void) {
    // require() and compile-cache state are runtime-owned, so module stays a
    // host service while node-core owns its public Node specifier.
    return node_core_host_namespace("module");
}

static Item node_core_buffer_namespace(void) {
    // The typed-array implementation remains host-owned during its staged
    // extraction; node-core owns the public builtin and global activation.
    return node_core_host_namespace("buffer");
}

static Item node_core_host_namespace_property(const char* specifier, const char* property) {
    Item namespace_item = node_core_host_namespace(specifier);
    if (!node_core_host || !node_core_host->value || !node_core_host->value->property_get ||
            !property || namespace_item.item == 0 || !node_core_host->node ||
            !node_core_host->node->roots || !node_core_host->node->roots->root_frame_begin ||
            !node_core_host->node->roots->root_frame_take_slot ||
            !node_core_host->node->roots->root_frame_end) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_core_host->node->roots->root_frame_begin(&frame, 2)) return ItemNull;
    uint64_t* namespace_root = node_core_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_core_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root) {
        node_core_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *namespace_root = namespace_item.item;
    Item key = node_core_host->value->string_from_utf8_n(property, strlen(property));
    *key_root = key.item;
    // Property-key creation may compact both operands before the lookup.
    Item result = node_core_host->value->property_get(
        (Item){.item = *namespace_root}, (Item){.item = *key_root});
    node_core_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_core_util_namespace(void) { return node_core_host_namespace("util"); }
static Item node_core_util_types_namespace(void) {
    return node_core_host_namespace_property("util", "types");
}
static Item node_core_inherits_namespace(void) {
    return node_core_host_namespace_property("util", "inherits");
}

static Item node_core_assert_namespace(void) { return node_core_host_namespace("assert"); }
static Item node_core_assert_strict_namespace(void) {
    return node_core_host_namespace_property("assert", "strict");
}
static Item node_core_stream_namespace(void) { return node_core_host_namespace("stream"); }
static Item node_core_stream_promises_namespace(void) {
    return node_core_host_namespace("stream/promises");
}
static Item node_core_stream_web_namespace(void) {
    return node_core_host_namespace("stream/web");
}
static Item node_core_stream_consumers_namespace(void) {
    return node_core_host_namespace("stream/consumers");
}
static Item node_core_stream_iter_namespace(void) {
    return node_core_host_namespace("stream/iter");
}
static Item node_core_repl_namespace(void) { return node_core_host_namespace("repl"); }
static Item node_core_diagnostics_channel_namespace(void) {
    return node_core_host_namespace("diagnostics_channel");
}

// These public names remain host-implemented until their leaf modules move,
// but node-core owns their profile-gated Jube descriptors now. Keeping the
// bridge here removes their parallel js_runtime dispatcher ownership.
#define NODE_CORE_HOST_NAMESPACE(function_name, host_specifier) \
    static Item function_name(void) { return node_core_host_namespace(host_specifier); }

NODE_CORE_HOST_NAMESPACE(node_core_internal_fs_promises_namespace, "internal/fs/promises")
NODE_CORE_HOST_NAMESPACE(node_core_internal_fs_utils_namespace, "internal/fs/utils")
NODE_CORE_HOST_NAMESPACE(node_core_child_process_namespace, "child_process")
NODE_CORE_HOST_NAMESPACE(node_core_crypto_namespace, "crypto")
NODE_CORE_HOST_NAMESPACE(node_core_tls_namespace, "tls")
NODE_CORE_HOST_NAMESPACE(node_core_http_namespace, "http")
NODE_CORE_HOST_NAMESPACE(node_core_https_namespace, "https")
NODE_CORE_HOST_NAMESPACE(node_core_internal_errors_namespace, "internal/errors")
NODE_CORE_HOST_NAMESPACE(node_core_internal_assert_myers_diff_namespace, "internal/assert/myers_diff")
NODE_CORE_HOST_NAMESPACE(node_core_internal_async_hooks_namespace, "internal/async_hooks")
NODE_CORE_HOST_NAMESPACE(node_core_internal_async_context_frame_namespace, "internal/async_context_frame")
NODE_CORE_HOST_NAMESPACE(node_core_internal_stream_add_abort_signal_namespace, "internal/streams/add-abort-signal")
NODE_CORE_HOST_NAMESPACE(node_core_internal_stream_end_of_stream_namespace, "internal/streams/end-of-stream")
NODE_CORE_HOST_NAMESPACE(node_core_internal_stream_state_namespace, "internal/streams/state")
NODE_CORE_HOST_NAMESPACE(node_core_internal_crypto_util_namespace, "internal/crypto/util")
NODE_CORE_HOST_NAMESPACE(node_core_internal_util_namespace, "internal/util")
NODE_CORE_HOST_NAMESPACE(node_core_internal_util_inspect_namespace, "internal/util/inspect")
NODE_CORE_HOST_NAMESPACE(node_core_internal_repl_namespace, "internal/repl")
NODE_CORE_HOST_NAMESPACE(node_core_internal_test_binding_namespace, "internal/test/binding")

#undef NODE_CORE_HOST_NAMESPACE

static Item node_core_vm_namespace(void) {
    // vm creates execution contexts directly, so its engine implementation
    // remains behind the explicit host-namespace service boundary.
    return node_core_host_namespace("vm");
}

static Item node_core_async_hooks_namespace(void) {
    // Async-resource bookkeeping is shared with the event loop and remains a
    // runtime service until its owning state can move as one unit.
    return node_core_host_namespace("async_hooks");
}

static Item node_core_trace_events_namespace(void) {
    // Trace buffers are runtime-global, so node-core exposes this host service
    // without importing its event-loop state directly.
    return node_core_host_namespace("trace_events");
}

static Item node_core_domain_namespace(void) {
    // Domains are entered by host event-loop delivery, so their active-stack
    // state remains runtime-owned behind this namespace bridge.
    return node_core_host_namespace("domain");
}

static Item node_core_cluster_namespace(void) {
    // Cluster process selection and IPC are owned by the host process layer.
    return node_core_host_namespace("cluster");
}

static Item node_core_readline_namespace(void) {
    return node_core_host_namespace("readline");
}

static Item node_core_readline_promises_namespace(void) {
    return node_core_host_namespace("readline/promises");
}

static Item node_core_test_namespace(void) {
    // The test runner is integrated with host process exit handling.
    return node_core_host_namespace("test");
}

static Item node_core_global_namespace(const char* name) {
    if (!node_core_host || !node_core_session || !node_core_host->node ||
            !node_core_host->node->runtime || !node_core_host->node->roots ||
            !node_core_host->node->runtime->session_is_live ||
            !node_core_host->node->runtime->session_is_live(node_core_session) ||
            !node_core_host->value || !node_core_host->value->string_from_utf8_n ||
            !node_core_host->script || !node_core_host->script->global_property) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_core_host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* key_root = node_core_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root) {
        node_core_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item key = node_core_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    // Key creation can compact the heap; read through the opaque root slot
    // before the property lookup observes the global object.
    key = (Item){.item = *key_root};
    Item result = node_core_host->script->global_property(key);
    node_core_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_core_console_namespace(void) {
    return node_core_global_namespace("console");
}

static Item node_core_process_namespace(void) {
    if (!node_core_host || !node_core_session || !node_core_host->node ||
            !node_core_host->node->runtime || !node_core_host->node->runtime->session_process) {
        return ItemNull;
    }
    return node_core_host->node->runtime->session_process(node_core_session);
}

static Item node_core_os_global(void* session) {
    (void)session;
    return node_core_os_namespace();
}

static Item node_core_buffer_global(void* session) {
    (void)session;
    // Buffer remains host-implemented during its staged migration, but its
    // global must only exist while the node-core profile is active.
    Item buffer_namespace = node_core_host_namespace("buffer");
    if (!node_core_host || !node_core_host->value || !node_core_host->value->property_get ||
            buffer_namespace.item == 0) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_core_host->node || !node_core_host->node->roots ||
            !node_core_host->node->roots->root_frame_begin ||
            !node_core_host->node->roots->root_frame_take_slot ||
            !node_core_host->node->roots->root_frame_end ||
            !node_core_host->node->roots->root_frame_begin(&frame, 2)) return ItemNull;
    uint64_t* namespace_root = node_core_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_core_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root) {
        node_core_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *namespace_root = buffer_namespace.item;
    Item key = node_core_host->value->string_from_utf8_n("Buffer", 6);
    *key_root = key.item;
    // Constructing the key can compact the namespace before property lookup.
    Item result = node_core_host->value->property_get(
        (Item){.item = *namespace_root}, (Item){.item = *key_root});
    node_core_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_core_vm_global(void* session) {
    (void)session;
    // vm is a compatibility global only in an active Node profile; the host
    // implementation remains reachable through the named resolver service.
    return node_core_vm_namespace();
}

static const JubeGlobalDef node_core_globals[] = {
    {"os", 0, node_core_os_global},
    {"Buffer", 0, node_core_buffer_global},
    {"vm", 0, node_core_vm_global},
};

static const char* const node_core_path_specifiers[] = {
    "path",
    "path/posix",
};

static const char* const node_core_path_win32_specifiers[] = {
    "path/win32",
};

static const char* const node_core_string_decoder_specifiers[] = {
    "string_decoder",
};

static const char* const node_core_querystring_specifiers[] = {
    "querystring",
};

static const char* const node_core_os_specifiers[] = {
    "os",
};

static const char* const node_core_url_specifiers[] = {
    "url",
};

static const char* const node_core_events_specifiers[] = {
    "events",
};

static const char* const node_core_punycode_specifiers[] = {
    "punycode",
};

static const char* const node_core_console_specifiers[] = {
    "console",
};

static const char* const node_core_process_specifiers[] = {
    "process",
};

static const char* const node_core_timers_promises_specifiers[] = {
    "timers/promises",
};

static const char* const node_core_timers_specifiers[] = {
    "timers",
};

static const char* const node_core_constants_specifiers[] = {
    "constants",
};
static const char* const node_core_v8_specifiers[] = { "v8" };
static const char* const node_core_perf_hooks_specifiers[] = { "perf_hooks" };
static const char* const node_core_workers_specifiers[] = { "worker_threads" };
static const char* const node_core_tty_specifiers[] = { "tty" };
static const char* const node_core_module_specifiers[] = { "module" };
static const char* const node_core_vm_specifiers[] = { "vm" };
static const char* const node_core_async_hooks_specifiers[] = { "async_hooks" };
static const char* const node_core_trace_events_specifiers[] = { "trace_events" };
static const char* const node_core_domain_specifiers[] = { "domain" };
static const char* const node_core_cluster_specifiers[] = { "cluster" };
static const char* const node_core_readline_specifiers[] = { "readline" };
static const char* const node_core_readline_promises_specifiers[] = { "readline/promises" };
static const char* const node_core_test_specifiers[] = { "node:test" };
static const char* const node_core_buffer_specifiers[] = { "buffer" };
static const char* const node_core_util_specifiers[] = { "util", "sys" };
static const char* const node_core_util_types_specifiers[] = { "util/types" };
static const char* const node_core_inherits_specifiers[] = { "inherits" };
static const char* const node_core_assert_specifiers[] = { "assert" };
static const char* const node_core_assert_strict_specifiers[] = { "assert/strict" };
static const char* const node_core_stream_specifiers[] = { "stream" };
static const char* const node_core_stream_promises_specifiers[] = { "stream/promises" };
static const char* const node_core_stream_web_specifiers[] = { "stream/web" };
static const char* const node_core_stream_consumers_specifiers[] = { "stream/consumers" };
static const char* const node_core_stream_iter_specifiers[] = { "stream/iter" };
static const char* const node_core_repl_specifiers[] = { "repl" };
static const char* const node_core_diagnostics_channel_specifiers[] = { "diagnostics_channel" };
static const char* const node_core_internal_fs_promises_specifiers[] = { "internal/fs/promises" };
static const char* const node_core_internal_fs_utils_specifiers[] = { "internal/fs/utils" };
static const char* const node_core_child_process_specifiers[] = { "child_process" };
static const char* const node_core_crypto_specifiers[] = { "crypto" };
static const char* const node_core_tls_specifiers[] = { "tls" };
static const char* const node_core_http_specifiers[] = {
    "http", "_http_agent", "_http_common", "_http_server", "_http_outgoing",
};
static const char* const node_core_https_specifiers[] = { "https" };
static const char* const node_core_internal_errors_specifiers[] = { "internal/errors" };
static const char* const node_core_internal_assert_myers_diff_specifiers[] = {
    "internal/assert/myers_diff",
};
static const char* const node_core_internal_async_hooks_specifiers[] = { "internal/async_hooks" };
static const char* const node_core_internal_async_context_frame_specifiers[] = {
    "internal/async_context_frame",
};
static const char* const node_core_internal_stream_add_abort_signal_specifiers[] = {
    "internal/streams/add-abort-signal",
};
static const char* const node_core_internal_stream_end_of_stream_specifiers[] = {
    "internal/streams/end-of-stream",
};
static const char* const node_core_internal_stream_state_specifiers[] = { "internal/streams/state" };
static const char* const node_core_internal_crypto_util_specifiers[] = { "internal/crypto/util" };
static const char* const node_core_internal_util_specifiers[] = { "internal/util" };
static const char* const node_core_internal_util_inspect_specifiers[] = { "internal/util/inspect" };
static const char* const node_core_internal_repl_specifiers[] = { "internal/repl" };
static const char* const node_core_internal_test_binding_specifiers[] = { "internal/test/binding" };

static const JubeNamespaceDef node_core_namespaces[] = {
    {node_core_path_specifiers, 2, node_core_path_namespace, NULL, 0},
    {node_core_path_win32_specifiers, 1, node_core_path_win32_namespace, NULL, 0},
    {node_core_string_decoder_specifiers, 1, node_core_string_decoder_namespace, NULL, 0},
    {node_core_querystring_specifiers, 1, node_core_querystring_namespace, NULL, 0},
    {node_core_os_specifiers, 1, node_core_os_namespace, NULL, 0},
    {node_core_url_specifiers, 1, node_core_url_namespace, NULL, 0},
    {node_core_events_specifiers, 1, node_core_events_namespace, NULL, 0},
    {node_core_punycode_specifiers, 1, node_core_punycode_namespace, NULL, 0},
    {node_core_console_specifiers, 1, node_core_console_namespace, NULL, 0},
    {node_core_process_specifiers, 1, node_core_process_namespace, NULL, 0},
    {node_core_timers_promises_specifiers, 1, node_core_timers_promises_namespace, NULL, 0},
    {node_core_timers_specifiers, 1, node_core_timers_namespace, NULL, 0},
    {node_core_constants_specifiers, 1, node_core_constants_namespace, NULL, 0},
    {node_core_v8_specifiers, 1, node_core_v8_namespace, NULL, 0},
    {node_core_perf_hooks_specifiers, 1, node_core_perf_hooks_namespace, NULL, 0},
    {node_core_workers_specifiers, 1, node_core_workers_namespace, NULL, 0},
    {node_core_tty_specifiers, 1, node_core_tty_namespace, NULL, 0},
    {node_core_module_specifiers, 1, node_core_module_namespace, NULL, 0},
    {node_core_vm_specifiers, 1, node_core_vm_namespace, NULL, 0},
    {node_core_async_hooks_specifiers, 1, node_core_async_hooks_namespace, NULL, 0},
    {node_core_trace_events_specifiers, 1, node_core_trace_events_namespace, NULL, 0},
    {node_core_domain_specifiers, 1, node_core_domain_namespace, NULL, 0},
    {node_core_cluster_specifiers, 1, node_core_cluster_namespace, NULL, 0},
    {node_core_readline_specifiers, 1, node_core_readline_namespace, NULL, 0},
    {node_core_readline_promises_specifiers, 1, node_core_readline_promises_namespace, NULL, 0},
    {node_core_test_specifiers, 1, node_core_test_namespace, NULL, 0},
    {node_core_buffer_specifiers, 1, node_core_buffer_namespace, NULL, 0},
    {node_core_util_specifiers, 2, node_core_util_namespace, NULL, 0},
    {node_core_util_types_specifiers, 1, node_core_util_types_namespace, NULL, 0},
    {node_core_inherits_specifiers, 1, node_core_inherits_namespace, NULL, 0},
    {node_core_assert_specifiers, 1, node_core_assert_namespace, NULL, 0},
    {node_core_assert_strict_specifiers, 1, node_core_assert_strict_namespace, NULL, 0},
    {node_core_stream_specifiers, 1, node_core_stream_namespace, NULL, 0},
    {node_core_stream_promises_specifiers, 1, node_core_stream_promises_namespace, NULL, 0},
    {node_core_stream_web_specifiers, 1, node_core_stream_web_namespace, NULL, 0},
    {node_core_stream_consumers_specifiers, 1, node_core_stream_consumers_namespace, NULL, 0},
    {node_core_stream_iter_specifiers, 1, node_core_stream_iter_namespace, NULL, 0},
    {node_core_repl_specifiers, 1, node_core_repl_namespace, NULL, 0},
    {node_core_diagnostics_channel_specifiers, 1, node_core_diagnostics_channel_namespace, NULL, 0},
    {node_core_internal_fs_promises_specifiers, 1, node_core_internal_fs_promises_namespace, NULL, 0},
    {node_core_internal_fs_utils_specifiers, 1, node_core_internal_fs_utils_namespace, NULL, 0},
    {node_core_child_process_specifiers, 1, node_core_child_process_namespace, NULL, 0},
    {node_core_crypto_specifiers, 1, node_core_crypto_namespace, NULL, 0},
    {node_core_tls_specifiers, 1, node_core_tls_namespace, NULL, 0},
    {node_core_http_specifiers, 5, node_core_http_namespace, NULL, 0},
    {node_core_https_specifiers, 1, node_core_https_namespace, NULL, 0},
    {node_core_internal_errors_specifiers, 1, node_core_internal_errors_namespace, NULL, 0},
    {node_core_internal_assert_myers_diff_specifiers, 1, node_core_internal_assert_myers_diff_namespace, NULL, 0},
    {node_core_internal_async_hooks_specifiers, 1, node_core_internal_async_hooks_namespace, NULL, 0},
    {node_core_internal_async_context_frame_specifiers, 1, node_core_internal_async_context_frame_namespace, NULL, 0},
    {node_core_internal_stream_add_abort_signal_specifiers, 1, node_core_internal_stream_add_abort_signal_namespace, NULL, 0},
    {node_core_internal_stream_end_of_stream_specifiers, 1, node_core_internal_stream_end_of_stream_namespace, NULL, 0},
    {node_core_internal_stream_state_specifiers, 1, node_core_internal_stream_state_namespace, NULL, 0},
    {node_core_internal_crypto_util_specifiers, 1, node_core_internal_crypto_util_namespace, NULL, 0},
    {node_core_internal_util_specifiers, 1, node_core_internal_util_namespace, NULL, 0},
    {node_core_internal_util_inspect_specifiers, 1, node_core_internal_util_inspect_namespace, NULL, 0},
    {node_core_internal_repl_specifiers, 1, node_core_internal_repl_namespace, NULL, 0},
    {node_core_internal_test_binding_specifiers, 1, node_core_internal_test_binding_namespace, NULL, 0},
};

static const JubeModuleRequirements node_core_requirements = {
    sizeof(JubeModuleRequirements),
    JUBE_HOST_API_VERSION,
    (uint32_t)(offsetof(JubeHostAPI, node) + sizeof(((JubeHostAPI*)NULL)->node)),
    0,
    JUBE_HOST_CAP_NODE_RUNTIME,
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNodeAPI),
    sizeof(JubeHostValueAPI),
    sizeof(JubeHostScriptAPI),
    sizeof(JubeHostRootAPI),
};

static int node_core_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime ||
            !host->node->runtime->resolve_host_namespace ||
            !host->node->runtime->session_process || !host->node->roots ||
            !host->node->roots->root_frame_begin || !host->node->roots->root_frame_take_slot ||
            !host->node->roots->root_frame_end || !host->value ||
            !host->value->string_from_utf8_n || !host->script ||
            !host->script->global_property) {
        return -1;
    }
    node_core_host = host;
    if (node_path_init(host) != 0) return -1;
    if (node_string_decoder_init(host) != 0) {
        node_path_shutdown();
        return -1;
    }
    if (node_querystring_init(host) != 0) {
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_os_init(host) != 0) {
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_url_init(host) != 0) {
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_events_init(host) != 0) {
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_punycode_init(host) != 0) {
        node_events_shutdown();
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_timers_init(host) != 0) {
        node_punycode_shutdown();
        node_events_shutdown();
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_constants_init(host) != 0) {
        node_timers_shutdown();
        node_punycode_shutdown();
        node_events_shutdown();
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_v8_init(host) != 0) {
        node_constants_shutdown();
        node_timers_shutdown();
        node_punycode_shutdown();
        node_events_shutdown();
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_perf_hooks_init(host) != 0) {
        node_v8_shutdown();
        node_constants_shutdown();
        node_timers_shutdown();
        node_punycode_shutdown();
        node_events_shutdown();
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_workers_init(host) != 0) {
        node_perf_hooks_shutdown();
        node_v8_shutdown();
        node_constants_shutdown();
        node_timers_shutdown();
        node_punycode_shutdown();
        node_events_shutdown();
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_tty_init(host) != 0) {
        node_workers_shutdown();
        node_perf_hooks_shutdown();
        node_v8_shutdown();
        node_constants_shutdown();
        node_timers_shutdown();
        node_punycode_shutdown();
        node_events_shutdown();
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    if (node_process_init(host) != 0) {
        node_tty_shutdown();
        node_workers_shutdown();
        node_perf_hooks_shutdown();
        node_v8_shutdown();
        node_constants_shutdown();
        node_timers_shutdown();
        node_punycode_shutdown();
        node_events_shutdown();
        node_url_shutdown();
        node_os_shutdown();
        node_querystring_shutdown();
        node_string_decoder_shutdown();
        node_path_shutdown();
        return -1;
    }
    return 0;
}

static void node_core_shutdown(void) {
    node_process_shutdown();
    node_tty_shutdown();
    node_workers_shutdown();
    node_perf_hooks_shutdown();
    node_v8_shutdown();
    node_constants_shutdown();
    node_timers_shutdown();
    node_punycode_shutdown();
    node_events_shutdown();
    node_url_shutdown();
    node_os_shutdown();
    node_querystring_shutdown();
    node_string_decoder_shutdown();
    node_path_shutdown();
    node_core_session = NULL;
    node_core_host = NULL;
}

static void node_core_runtime_attach(void* session) {
    if (!node_core_host || !node_core_host->node || !node_core_host->node->runtime ||
            !node_core_host->node->runtime->session_is_live ||
            !node_core_host->node->runtime->session_is_live(session)) {
        return;
    }
    node_core_session = session;
    node_path_runtime_attach(session);
    node_string_decoder_runtime_attach(session);
    node_querystring_runtime_attach(session);
    node_os_runtime_attach(session);
    node_url_runtime_attach(session);
    node_events_runtime_attach(session);
    node_punycode_runtime_attach(session);
    node_timers_runtime_attach(session);
    node_constants_runtime_attach(session);
    node_v8_runtime_attach(session);
    node_perf_hooks_runtime_attach(session);
    node_workers_runtime_attach(session);
    node_tty_runtime_attach(session);
    node_process_runtime_attach(session);
    // Activate util's formatter hook before console output can observe the
    // Node profile; minimal never attaches node-core and uses the hook's
    // deliberate generic formatter fallback.
    (void)node_core_util_namespace();
}

static void node_core_runtime_reset(void* session) {
    if (session != node_core_session) return;
    // The path bridge keeps no module-owned Item cache; host path reset owns it.
    node_path_runtime_reset(session);
    node_string_decoder_runtime_reset(session);
    node_querystring_runtime_reset(session);
    node_os_runtime_reset(session);
    node_url_runtime_reset(session);
    node_events_runtime_reset(session);
    node_punycode_runtime_reset(session);
    node_timers_runtime_reset(session);
    node_constants_runtime_reset(session);
    node_v8_runtime_reset(session);
    node_perf_hooks_runtime_reset(session);
    node_workers_runtime_reset(session);
    node_tty_runtime_reset(session);
    node_process_runtime_reset(session);
}

static void node_core_runtime_detach(void* session) {
    node_path_runtime_detach(session);
    node_string_decoder_runtime_detach(session);
    node_querystring_runtime_detach(session);
    node_os_runtime_detach(session);
    node_url_runtime_detach(session);
    node_events_runtime_detach(session);
    node_punycode_runtime_detach(session);
    node_timers_runtime_detach(session);
    node_constants_runtime_detach(session);
    node_v8_runtime_detach(session);
    node_perf_hooks_runtime_detach(session);
    node_workers_runtime_detach(session);
    node_tty_runtime_detach(session);
    node_process_runtime_detach(session);
    if (session == node_core_session) node_core_session = NULL;
}

static const JubeModuleDef node_core_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "node-core",
    "0.1.0",
    "Node compatibility core namespace bridge",
    NULL,
    0,
    NULL,
    0,
    node_core_namespaces,
    58,
    node_core_init,
    node_core_shutdown,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    &node_core_requirements,
    node_core_globals,
    3,
    node_core_runtime_attach,
    node_core_runtime_reset,
    node_core_runtime_detach,
};

#if !defined(LAMBDA_NODE_CORE_DYNAMIC_MODULE)
extern "C" void node_core_jube_register_static(void) {
    jube_register_static_module(&node_core_module);
}
#endif

extern "C" const JubeModuleDef* node_core_jube_module(void) {
    return &node_core_module;
}

// Dynamic module images use the generic loader entry; the static executable
// calls node_core_jube_register_static instead and never resolves this name.
#if defined(LAMBDA_NODE_CORE_DYNAMIC_MODULE)
extern "C" const JubeModuleDef* jube_module(void) {
    return node_core_jube_module();
}
#endif
