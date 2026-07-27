// node_perf_hooks.cpp — node:perf_hooks facade through Jube value and script services.
#include "node_perf_hooks.hpp"
#include "../../jube/jube_registry.h"

#include <cstring>

static const JubeHostAPI* node_perf_hooks_host = NULL;
struct NodePerfHooksSessionState { void* session; bool rooted; Item cached_namespace; };
static NodePerfHooksSessionState* node_perf_hooks_state(void) { return (NodePerfHooksSessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_PERF_HOOKS); }
#define node_perf_hooks_session (node_perf_hooks_state()->session)
#define node_perf_hooks_rooted (node_perf_hooks_state()->rooted)
#define node_perf_hooks_cached_namespace (node_perf_hooks_state()->cached_namespace)

static Item node_perf_hooks_empty_object(Item unused) {
    (void)unused;
    return node_perf_hooks_host->value->new_object();
}

static Item node_perf_hooks_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static void node_perf_hooks_set_property(Item object, const char* name, Item value) {
    JubeRootFrame frame = {};
    if (!node_perf_hooks_host->node->roots->root_frame_begin(&frame, 3)) return;
    uint64_t* object_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_perf_hooks_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    *value_root = value.item;
    Item key = node_perf_hooks_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    // Key allocation may move both the namespace and child value before publish.
    node_perf_hooks_host->value->property_set(node_perf_hooks_root_value(object_root),
        node_perf_hooks_root_value(key_root), node_perf_hooks_root_value(value_root));
    node_perf_hooks_host->node->roots->root_frame_end(&frame);
}

static void node_perf_hooks_set_method(Item object, const char* name, void* function) {
    JubeRootFrame frame = {};
    if (!node_perf_hooks_host->node->roots->root_frame_begin(&frame, 2)) return;
    uint64_t* object_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !function_root) {
        node_perf_hooks_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    Item method = node_perf_hooks_host->script->new_function(function, 1);
    *function_root = method.item;
    node_perf_hooks_set_property(node_perf_hooks_root_value(object_root), name,
        node_perf_hooks_root_value(function_root));
    node_perf_hooks_host->node->roots->root_frame_end(&frame);
}

static Item node_perf_hooks_global_property(const char* name) {
    JubeRootFrame frame = {};
    if (!node_perf_hooks_host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* key_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root) {
        node_perf_hooks_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item key = node_perf_hooks_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    // Keep the global key rooted because string construction can compact before lookup.
    Item result = node_perf_hooks_host->script->global_property(
        node_perf_hooks_root_value(key_root));
    node_perf_hooks_host->node->roots->root_frame_end(&frame);
    return result;
}

Item node_perf_hooks_namespace(void) {
    if (node_perf_hooks_cached_namespace.item != 0) return node_perf_hooks_cached_namespace;
    if (!node_perf_hooks_host || !node_perf_hooks_session) return ItemNull;

    JubeRootFrame frame = {};
    if (!node_perf_hooks_host->node->roots->root_frame_begin(&frame, 3)) return ItemNull;
    uint64_t* namespace_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* performance_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* observer_root = node_perf_hooks_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !performance_root || !observer_root) {
        node_perf_hooks_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    node_perf_hooks_cached_namespace = node_perf_hooks_host->value->new_object();
    *namespace_root = node_perf_hooks_cached_namespace.item;
    Item performance = node_perf_hooks_global_property("performance");
    *performance_root = performance.item;
    node_perf_hooks_set_property(node_perf_hooks_root_value(namespace_root), "performance",
        node_perf_hooks_root_value(performance_root));
    Item observer = node_perf_hooks_global_property("PerformanceObserver");
    *observer_root = observer.item;
    // Property-key allocation can compact after the global lookup, so retain
    // the observer until the persistent namespace owns the property value.
    node_perf_hooks_set_property(node_perf_hooks_root_value(namespace_root), "PerformanceObserver",
        node_perf_hooks_root_value(observer_root));
    node_perf_hooks_set_method(node_perf_hooks_root_value(namespace_root), "PerformanceEntry",
        (void*)node_perf_hooks_empty_object);
    node_perf_hooks_set_method(node_perf_hooks_root_value(namespace_root), "monitorEventLoopDelay",
        (void*)node_perf_hooks_empty_object);
    node_perf_hooks_set_method(node_perf_hooks_root_value(namespace_root), "createHistogram",
        (void*)node_perf_hooks_empty_object);
    node_perf_hooks_set_property(node_perf_hooks_root_value(namespace_root), "default",
        node_perf_hooks_root_value(namespace_root));
    node_perf_hooks_cached_namespace = node_perf_hooks_root_value(namespace_root);
    node_perf_hooks_host->node->roots->root_frame_end(&frame);
    return node_perf_hooks_cached_namespace;
}

int node_perf_hooks_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->roots || !host->value || !host->script ||
            !host->value->new_object || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->script->new_function ||
            !host->script->global_property) return -1;
    node_perf_hooks_host = host;
    return 0;
}

void node_perf_hooks_shutdown(void) {
    node_perf_hooks_host = NULL;
}

void node_perf_hooks_runtime_attach(void* session) {
    if (!node_perf_hooks_host || !node_perf_hooks_host->node->runtime ||
            !node_perf_hooks_host->node->runtime->session_is_live ||
            !node_perf_hooks_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_PERF_HOOKS,
            sizeof(NodePerfHooksSessionState))) return;
    node_perf_hooks_session = session;
    if (node_perf_hooks_host->node->roots->persistent_root_register(
            session, &node_perf_hooks_cached_namespace.item) == 0) {
        node_perf_hooks_rooted = true;
    }
}

void node_perf_hooks_runtime_reset(void* session) {
    if (session == node_perf_hooks_session) node_perf_hooks_cached_namespace = (Item){0};
}

void node_perf_hooks_runtime_detach(void* session) {
    if (!node_perf_hooks_host || session != node_perf_hooks_session) return;
    if (node_perf_hooks_rooted) {
        node_perf_hooks_host->node->roots->persistent_root_unregister(
            session, &node_perf_hooks_cached_namespace.item);
    }
    node_perf_hooks_rooted = false;
    node_perf_hooks_cached_namespace = (Item){0};
    node_perf_hooks_session = NULL;
}
