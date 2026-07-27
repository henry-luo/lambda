// node_net_module.cpp -- Node net/dns namespace ownership during stream migration.
#include "../../jube/jube.h"
#include "../../jube/jube_registry.h"

#include <cstring>
#include <climits>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif

static const JubeHostAPI* node_net_host = NULL;
static void* node_net_session = NULL;

static bool node_net_is_undefined_or_null(Item value) {
    return value.item == ITEM_JS_UNDEFINED || value.item == ItemNull.item;
}

static Item node_net_root_value(uint64_t* root) {
    return (Item){.item = root ? *root : ItemNull.item};
}

static Item node_net_property_key(uint64_t* key_root, const char* name) {
    if (!key_root || !node_net_host || !node_net_host->value ||
            !node_net_host->value->string_from_utf8_n || !name) return ItemNull;
    Item key = node_net_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    return key;
}

static void node_net_property_set_name(uint64_t* object_root, uint64_t* key_root,
                                       const char* name, Item value) {
    if (!object_root || !node_net_host || !node_net_host->value ||
            !node_net_host->value->property_set) return;
    Item key = node_net_property_key(key_root, name);
    if (key.item) node_net_host->value->property_set(node_net_root_value(object_root), key, value);
}

static Item node_net_normalize_args(Item input) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->roots ||
            !node_net_host->node->roots->root_frame_begin ||
            !node_net_host->node->roots->root_frame_take_slot ||
            !node_net_host->node->roots->root_frame_end || !node_net_host->value ||
            !node_net_host->value->array_new || !node_net_host->value->array_push ||
            !node_net_host->value->array_length || !node_net_host->value->array_get ||
            !node_net_host->value->new_object || !node_net_host->value->kind ||
            !node_net_host->value->property_set) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_net_host->node->roots->root_frame_begin(&frame, 5)) return ItemNull;
    uint64_t* input_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* result_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* options_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* callback_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    if (!input_root || !result_root || !options_root || !callback_root || !key_root) {
        node_net_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *input_root = input.item;
    *result_root = node_net_host->value->array_new(0).item;
    *options_root = node_net_host->value->new_object().item;
    *callback_root = ItemNull.item;
    *key_root = ItemNull.item;

    // This marker is consumed by the host connect parser, so its spelling is
    // an inter-module compatibility invariant rather than a private detail.
    if (node_net_host->value->is_array(node_net_root_value(input_root))) {
        int64_t length = node_net_host->value->array_length(node_net_root_value(input_root));
        if (length > 0) {
            Item first = node_net_host->value->array_get(node_net_root_value(input_root), 0);
            if (node_net_host->value->kind(first) == JUBE_VALUE_OBJECT) {
                *options_root = first.item;
            } else if (!node_net_is_undefined_or_null(first)) {
                node_net_property_set_name(options_root, key_root, "port", first);
            }
        }
        if (length > 1) {
            Item second = node_net_host->value->array_get(node_net_root_value(input_root), 1);
            if (node_net_host->value->kind(second) == JUBE_VALUE_FUNCTION) {
                *callback_root = second.item;
            } else if (!node_net_is_undefined_or_null(second) &&
                    node_net_host->value->kind(node_net_root_value(options_root)) == JUBE_VALUE_OBJECT) {
                node_net_property_set_name(options_root, key_root, "host", second);
            }
        }
        if (length > 2) {
            Item third = node_net_host->value->array_get(node_net_root_value(input_root), 2);
            if (node_net_host->value->kind(third) == JUBE_VALUE_FUNCTION) *callback_root = third.item;
        }
    }
    node_net_host->value->array_push(node_net_root_value(result_root), node_net_root_value(options_root));
    node_net_host->value->array_push(node_net_root_value(result_root), node_net_root_value(callback_root));
    node_net_property_set_name(result_root, key_root, "__lambda_net_normalized_args__",
        (Item){.item = ITEM_TRUE});
    Item result = node_net_root_value(result_root);
    node_net_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_net_is_ip(Item input) {
    if (!node_net_host || !node_net_host->value || !node_net_host->script ||
            !node_net_host->value->string_bytes || !node_net_host->value->string_length ||
            !node_net_host->script->make_number || !node_net_host->node ||
            !node_net_host->node->roots || !node_net_host->node->roots->root_frame_begin ||
            !node_net_host->node->roots->root_frame_take_slot ||
            !node_net_host->node->roots->root_frame_end) {
        return node_net_host && node_net_host->script && node_net_host->script->make_number ?
            node_net_host->script->make_number(0.0) : ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_net_host->node->roots->root_frame_begin(&frame, 1)) {
        return node_net_host->script->make_number(0.0);
    }
    uint64_t* input_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    if (!input_root) {
        node_net_host->node->roots->root_frame_end(&frame);
        return node_net_host->script->make_number(0.0);
    }
    *input_root = input.item;
    Item candidate = (Item){.item = *input_root};
    if (node_net_host->value->kind(candidate) == JUBE_VALUE_OBJECT) {
        candidate = node_net_host->script->to_string(candidate);
        *input_root = candidate.item;
    }
    if (node_net_host->value->kind((Item){.item = *input_root}) != JUBE_VALUE_STRING) {
        node_net_host->node->roots->root_frame_end(&frame);
        return node_net_host->script->make_number(0.0);
    }
    size_t length = node_net_host->value->string_length((Item){.item = *input_root});
    const uint8_t* bytes = node_net_host->value->string_bytes((Item){.item = *input_root});
    if (!bytes || length == 0 || length >= 256) {
        node_net_host->node->roots->root_frame_end(&frame);
        return node_net_host->script->make_number(0.0);
    }
    char address[256];
    memcpy(address, bytes, length);
    address[length] = '\0';
    struct in_addr ipv4 = {};
    Item result = node_net_host->script->make_number(0.0);
    if (inet_pton(AF_INET, address, &ipv4) == 1) {
        result = node_net_host->script->make_number(4.0);
    } else {
        struct in6_addr ipv6 = {};
        result = node_net_host->script->make_number(
            inet_pton(AF_INET6, address, &ipv6) == 1 ? 6.0 : 0.0);
    }
    node_net_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_net_is_ipv4(Item input) {
    if (!node_net_host || !node_net_host->script || !node_net_host->script->make_number) {
        return ItemNull;
    }
    return (Item){.item = b2it(node_net_host->script->get_number(node_net_is_ip(input)) == 4.0)};
}

static Item node_net_is_ipv6(Item input) {
    if (!node_net_host || !node_net_host->script || !node_net_host->script->make_number) {
        return ItemNull;
    }
    return (Item){.item = b2it(node_net_host->script->get_number(node_net_is_ip(input)) == 6.0)};
}

static Item node_net_get_default_auto_select_family(void) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->network) return ItemNull;
    return (Item){.item = b2it(node_net_host->node->network->default_auto_select_family_get())};
}

static Item node_net_set_default_auto_select_family(Item enabled) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->network ||
            !node_net_host->script || !node_net_host->script->is_truthy) return ItemNull;
    node_net_host->node->network->default_auto_select_family_set(
        node_net_host->script->is_truthy(enabled));
    return (Item){.item = ITEM_UNDEFINED};
}

static Item node_net_get_default_auto_select_family_timeout(void) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->network ||
            !node_net_host->script || !node_net_host->script->make_number) return ItemNull;
    return node_net_host->script->make_number(
        (double)node_net_host->node->network->default_auto_select_family_timeout_get());
}

static Item node_net_set_default_auto_select_family_timeout(Item timeout) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->network ||
            !node_net_host->value || !node_net_host->value->number_to_int64_exact ||
            !node_net_host->node->error || !node_net_host->node->error->throw_range_error_code) {
        return ItemNull;
    }
    int64_t timeout_ms = 0;
    if (!node_net_host->value->number_to_int64_exact(timeout, &timeout_ms) ||
            timeout_ms < 1 || timeout_ms > INT_MAX ||
            !node_net_host->node->network->default_auto_select_family_timeout_set((int)timeout_ms)) {
        return node_net_host->node->error->throw_range_error_code(node_net_session,
            "ERR_OUT_OF_RANGE",
            "The value of \"timeout\" is out of range. It must be a positive integer.");
    }
    return (Item){.item = ITEM_UNDEFINED};
}

static void node_net_set_method(uint64_t* namespace_root, uint64_t* key_root,
        uint64_t* function_root, const char* name, void* function, int parameter_count) {
    if (!namespace_root || !key_root || !function_root || !node_net_host ||
            !node_net_host->value || !node_net_host->script) return;
    Item key = node_net_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item method = node_net_host->script->new_function(function, parameter_count);
    *function_root = method.item;
    node_net_host->value->property_set((Item){.item = *namespace_root},
        (Item){.item = *key_root}, (Item){.item = *function_root});
}

static Item node_net_install_ip_helpers(Item namespace_item) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->roots ||
            !node_net_host->node->roots->root_frame_begin ||
            !node_net_host->node->roots->root_frame_take_slot ||
            !node_net_host->node->roots->root_frame_end || !node_net_host->value ||
            !node_net_host->value->string_from_utf8_n || !node_net_host->value->property_set ||
            !node_net_host->script || !node_net_host->script->new_function) return namespace_item;
    JubeRootFrame frame = {};
    if (!node_net_host->node->roots->root_frame_begin(&frame, 3)) return namespace_item;
    uint64_t* namespace_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root || !function_root) {
        node_net_host->node->roots->root_frame_end(&frame);
        return namespace_item;
    }
    *namespace_root = namespace_item.item;
    // IP literal parsing is platform-only; publishing it here keeps this
    // Node-visible surface out of js_net while stream ownership moves later.
    node_net_set_method(namespace_root, key_root, function_root, "isIP", (void*)node_net_is_ip, 1);
    node_net_set_method(namespace_root, key_root, function_root, "isIPv4", (void*)node_net_is_ipv4, 1);
    node_net_set_method(namespace_root, key_root, function_root, "isIPv6", (void*)node_net_is_ipv6, 1);
    node_net_set_method(namespace_root, key_root, function_root, "getDefaultAutoSelectFamily",
        (void*)node_net_get_default_auto_select_family, 0);
    node_net_set_method(namespace_root, key_root, function_root, "setDefaultAutoSelectFamily",
        (void*)node_net_set_default_auto_select_family, 1);
    node_net_set_method(namespace_root, key_root, function_root,
        "getDefaultAutoSelectFamilyAttemptTimeout",
        (void*)node_net_get_default_auto_select_family_timeout, 0);
    node_net_set_method(namespace_root, key_root, function_root,
        "setDefaultAutoSelectFamilyAttemptTimeout",
        (void*)node_net_set_default_auto_select_family_timeout, 1);
    node_net_set_method(namespace_root, key_root, function_root, "_normalizeArgs",
        (void*)node_net_normalize_args, 1);
    Item result = (Item){.item = *namespace_root};
    node_net_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_net_host_namespace(const char* specifier) {
    if (!node_net_host || !node_net_session || !node_net_host->node ||
            !node_net_host->node->runtime ||
            !node_net_host->node->runtime->resolve_host_namespace || !specifier) {
        return ItemNull;
    }
    Item result = ItemNull;
    return node_net_host->node->runtime->resolve_host_namespace(node_net_session, specifier,
        &result) == 0 ? result : ItemNull;
}

static Item node_net_namespace(void) {
    return node_net_install_ip_helpers(node_net_host_namespace("net"));
}
static Item node_net_internal_namespace(void) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->roots ||
            !node_net_host->node->roots->root_frame_begin ||
            !node_net_host->node->roots->root_frame_take_slot ||
            !node_net_host->node->roots->root_frame_end || !node_net_host->value ||
            !node_net_host->value->new_object || !node_net_host->value->property_set) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_net_host->node->roots->root_frame_begin(&frame, 3)) return ItemNull;
    uint64_t* namespace_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root || !value_root) {
        node_net_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *namespace_root = node_net_host->value->new_object().item;
    *value_root = node_net_host->value->string_from_utf8_n("__lambda_net_normalized_args__",
        strlen("__lambda_net_normalized_args__")).item;
    node_net_property_set_name(namespace_root, key_root, "normalizedArgsSymbol",
        node_net_root_value(value_root));
    *value_root = node_net_host->value->string_from_utf8_n("kReinitializeHandle",
        strlen("kReinitializeHandle")).item;
    node_net_property_set_name(namespace_root, key_root, "kReinitializeHandle",
        node_net_root_value(value_root));
    Item result = node_net_root_value(namespace_root);
    node_net_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_net_dns_lookup_sync(Item hostname) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->roots ||
            !node_net_host->node->roots->root_frame_begin ||
            !node_net_host->node->roots->root_frame_take_slot ||
            !node_net_host->node->roots->root_frame_end || !node_net_host->value ||
            !node_net_host->value->kind || !node_net_host->value->string_bytes ||
            !node_net_host->value->string_length || !node_net_host->value->string_from_utf8_n ||
            node_net_host->value->kind(hostname) != JUBE_VALUE_STRING) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_net_host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* hostname_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    if (!hostname_root) {
        node_net_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *hostname_root = hostname.item;
    size_t hostname_length = node_net_host->value->string_length(node_net_root_value(hostname_root));
    const uint8_t* hostname_bytes = node_net_host->value->string_bytes(node_net_root_value(hostname_root));
    if (!hostname_bytes || hostname_length == 0 || hostname_length >= 256) {
        node_net_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    char hostname_buffer[256];
    memcpy(hostname_buffer, hostname_bytes, hostname_length);
    hostname_buffer[hostname_length] = '\0';

    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result_info = NULL;
    int status = getaddrinfo(hostname_buffer, NULL, &hints, &result_info);
    if (status != 0 || !result_info) {
        if (result_info) freeaddrinfo(result_info);
        node_net_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    char address[INET6_ADDRSTRLEN] = {};
    if (result_info->ai_family == AF_INET) {
        const struct sockaddr_in* socket_address = (const struct sockaddr_in*)result_info->ai_addr;
        (void)inet_ntop(AF_INET, &socket_address->sin_addr, address, sizeof(address));
    } else if (result_info->ai_family == AF_INET6) {
        const struct sockaddr_in6* socket_address = (const struct sockaddr_in6*)result_info->ai_addr;
        (void)inet_ntop(AF_INET6, &socket_address->sin6_addr, address, sizeof(address));
    }
    freeaddrinfo(result_info);
    Item output = address[0] ? node_net_host->value->string_from_utf8_n(address, strlen(address)) : ItemNull;
    node_net_host->node->roots->root_frame_end(&frame);
    return output;
}

static Item node_net_install_dns_helpers(Item namespace_item) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->roots ||
            !node_net_host->node->roots->root_frame_begin ||
            !node_net_host->node->roots->root_frame_take_slot ||
            !node_net_host->node->roots->root_frame_end) return namespace_item;
    JubeRootFrame frame = {};
    if (!node_net_host->node->roots->root_frame_begin(&frame, 3)) return namespace_item;
    uint64_t* namespace_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_net_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root || !function_root) {
        node_net_host->node->roots->root_frame_end(&frame);
        return namespace_item;
    }
    *namespace_root = namespace_item.item;
    // lookupSync has no loop state: keeping its resolver implementation here
    // removes this real DNS operation from the host adapter without exposing uv.
    node_net_set_method(namespace_root, key_root, function_root, "lookupSync",
        (void*)node_net_dns_lookup_sync, 1);
    Item result = node_net_root_value(namespace_root);
    node_net_host->node->roots->root_frame_end(&frame);
    return result;
}
static Item node_net_internal_socket_namespace(void) {
    return node_net_host_namespace("internal/js_stream_socket");
}
static Item node_net_dns_namespace(void) {
    return node_net_install_dns_helpers(node_net_host_namespace("dns"));
}
static Item node_net_dns_promises_namespace(void) {
    return node_net_host_namespace("dns/promises");
}

static const char* const node_net_specifiers[] = { "net" };
static const char* const node_net_internal_specifiers[] = { "internal/net" };
static const char* const node_net_internal_socket_specifiers[] = {
    "internal/js_stream_socket",
};
static const char* const node_net_dns_specifiers[] = { "dns" };
static const char* const node_net_dns_promises_specifiers[] = { "dns/promises" };

static const JubeNamespaceDef node_net_namespaces[] = {
    {node_net_specifiers, 1, node_net_namespace, NULL, 0},
    {node_net_internal_specifiers, 1, node_net_internal_namespace, NULL, 0},
    {node_net_internal_socket_specifiers, 1, node_net_internal_socket_namespace, NULL, 0},
    {node_net_dns_specifiers, 1, node_net_dns_namespace, NULL, 0},
    {node_net_dns_promises_specifiers, 1, node_net_dns_promises_namespace, NULL, 0},
};

static const JubeModuleRequirements node_net_requirements = {
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

static int node_net_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime ||
            !host->node->runtime->resolve_host_namespace ||
            !host->node->runtime->session_is_live || !host->node->roots ||
            !host->node->roots->root_frame_begin || !host->node->roots->root_frame_take_slot ||
            !host->node->roots->root_frame_end || !host->value || !host->value->kind ||
            !host->value->string_bytes || !host->value->string_length ||
            !host->value->string_from_utf8_n || !host->value->property_set || !host->script ||
            !host->script->new_function || !host->script->make_number || !host->script->to_string ||
            !host->script->get_number || !host->script->is_truthy || !host->node->network ||
            !host->node->network->default_auto_select_family_get ||
            !host->node->network->default_auto_select_family_set ||
            !host->node->network->default_auto_select_family_timeout_get ||
            !host->node->network->default_auto_select_family_timeout_set ||
            !host->node->network->permission_has_net ||
            !host->node->network->permission_make_net_error || !host->node->error ||
            !host->node->error->throw_range_error_code || !host->value->number_to_int64_exact) return -1;
    node_net_host = host;
    return 0;
}

static void node_net_shutdown(void) {
    node_net_session = NULL;
    node_net_host = NULL;
}

static void node_net_runtime_attach(void* session) {
    if (!node_net_host || !node_net_host->node || !node_net_host->node->runtime ||
            !node_net_host->node->runtime->session_is_live ||
            !node_net_host->node->runtime->session_is_live(session)) return;
    node_net_session = session;
}

static void node_net_runtime_reset(void* session) {
    (void)session;
}

static void node_net_runtime_detach(void* session) {
    if (session == node_net_session) node_net_session = NULL;
}

static const JubeModuleDef node_net_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "node-net",
    "0.1.0",
    "Node net and dns namespace module",
    NULL,
    0,
    NULL,
    0,
    node_net_namespaces,
    5,
    node_net_init,
    node_net_shutdown,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    &node_net_requirements,
    NULL,
    0,
    node_net_runtime_attach,
    node_net_runtime_reset,
    node_net_runtime_detach,
};

#if !defined(LAMBDA_NODE_NET_DYNAMIC_MODULE)
extern "C" void node_net_jube_register_static(void) {
    jube_register_static_module(&node_net_module);
}
#endif

extern "C" const JubeModuleDef* node_net_jube_module(void) { return &node_net_module; }

#if defined(LAMBDA_NODE_NET_DYNAMIC_MODULE)
extern "C" const JubeModuleDef* jube_module(void) { return node_net_jube_module(); }
#endif
