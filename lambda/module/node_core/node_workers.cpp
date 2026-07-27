// node_workers.cpp — node:worker_threads namespace through Jube worker services.
#include "node_workers.hpp"
#include "../../jube/jube_registry.h"

#include <cstring>

static const JubeHostAPI* node_workers_host = NULL;
struct NodeWorkersSessionState { void* session; bool rooted; Item cached_namespace; };
static NodeWorkersSessionState* node_workers_state(void) { return (NodeWorkersSessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_WORKERS); }
#define node_workers_session (node_workers_state()->session)
#define node_workers_rooted (node_workers_state()->rooted)
#define node_workers_cached_namespace (node_workers_state()->cached_namespace)

static Item node_workers_empty_object(Item unused) {
    (void)unused;
    return node_workers_host->value->new_object();
}

static Item node_workers_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static void node_workers_set_property(Item object, const char* name, Item value) {
    JubeRootFrame frame = {};
    if (!node_workers_host->node->roots->root_frame_begin(&frame, 3)) return;
    uint64_t* object_root = node_workers_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_workers_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_workers_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_workers_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    *value_root = value.item;
    Item key = node_workers_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    // Creating the property key can compact before the namespace owns value.
    node_workers_host->value->property_set(node_workers_root_value(object_root),
        node_workers_root_value(key_root), node_workers_root_value(value_root));
    node_workers_host->node->roots->root_frame_end(&frame);
}

static void node_workers_set_method(Item object, const char* name, void* function,
                                    int parameter_count) {
    JubeRootFrame frame = {};
    if (!node_workers_host->node->roots->root_frame_begin(&frame, 2)) return;
    uint64_t* object_root = node_workers_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_workers_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !function_root) {
        node_workers_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    Item method = node_workers_host->script->new_function(function, parameter_count);
    *function_root = method.item;
    node_workers_set_property(node_workers_root_value(object_root), name,
        node_workers_root_value(function_root));
    node_workers_host->node->roots->root_frame_end(&frame);
}

Item node_workers_namespace(void) {
    if (node_workers_cached_namespace.item != 0) return node_workers_cached_namespace;
    if (!node_workers_host || !node_workers_session) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_workers_host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* namespace_root = node_workers_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root) {
        node_workers_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    node_workers_cached_namespace = node_workers_host->value->new_object();
    *namespace_root = node_workers_cached_namespace.item;
    node_workers_set_property(node_workers_root_value(namespace_root), "isMainThread",
        (Item){.item = ITEM_TRUE});
    node_workers_set_property(node_workers_root_value(namespace_root), "parentPort", ItemNull);
    node_workers_set_property(node_workers_root_value(namespace_root), "workerData", ItemNull);
    node_workers_set_method(node_workers_root_value(namespace_root), "MessageChannel",
        (void*)node_workers_host->node->workers->message_channel_new, 0);
    node_workers_set_method(node_workers_root_value(namespace_root), "MessagePort",
        (void*)node_workers_host->node->workers->message_port_new, 0);
    node_workers_set_method(node_workers_root_value(namespace_root), "moveMessagePortToContext",
        (void*)node_workers_host->node->workers->message_port_move_to_context, 2);
    node_workers_set_method(node_workers_root_value(namespace_root), "receiveMessageOnPort",
        (void*)node_workers_host->node->workers->receive_message_on_port, 1);
    node_workers_set_method(node_workers_root_value(namespace_root), "markAsUntransferable",
        (void*)node_workers_host->node->workers->mark_as_untransferable, 1);
    node_workers_set_method(node_workers_root_value(namespace_root), "isMarkedAsUntransferable",
        (void*)node_workers_host->node->workers->is_marked_as_untransferable, 1);
    node_workers_set_method(node_workers_root_value(namespace_root), "Worker",
        (void*)node_workers_empty_object, 1);
    node_workers_set_property(node_workers_root_value(namespace_root), "threadId",
        (Item){.item = i2it(0)});
    node_workers_set_method(node_workers_root_value(namespace_root), "BroadcastChannel",
        (void*)node_workers_empty_object, 1);
    node_workers_set_property(node_workers_root_value(namespace_root), "default",
        node_workers_root_value(namespace_root));
    node_workers_cached_namespace = node_workers_root_value(namespace_root);
    node_workers_host->node->roots->root_frame_end(&frame);
    return node_workers_cached_namespace;
}

int node_workers_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->roots || !host->node->workers ||
            host->node->workers->struct_size < sizeof(JubeHostWorkerAPI) || !host->value ||
            !host->value->new_object || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->script || !host->script->new_function ||
            !host->node->workers->message_channel_new || !host->node->workers->message_port_new ||
            !host->node->workers->message_port_move_to_context ||
            !host->node->workers->receive_message_on_port ||
            !host->node->workers->mark_as_untransferable ||
            !host->node->workers->is_marked_as_untransferable) return -1;
    node_workers_host = host;
    return 0;
}

void node_workers_shutdown(void) {
    node_workers_host = NULL;
}

void node_workers_runtime_attach(void* session) {
    if (!node_workers_host || !node_workers_host->node->runtime ||
            !node_workers_host->node->runtime->session_is_live ||
            !node_workers_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_WORKERS,
            sizeof(NodeWorkersSessionState))) return;
    node_workers_session = session;
    if (node_workers_host->node->roots->persistent_root_register(
            session, &node_workers_cached_namespace.item) == 0) node_workers_rooted = true;
}

void node_workers_runtime_reset(void* session) {
    if (session == node_workers_session) node_workers_cached_namespace = (Item){0};
}

void node_workers_runtime_detach(void* session) {
    if (!node_workers_host || session != node_workers_session) return;
    if (node_workers_rooted) {
        node_workers_host->node->roots->persistent_root_unregister(
            session, &node_workers_cached_namespace.item);
    }
    node_workers_rooted = false;
    node_workers_cached_namespace = (Item){0};
    node_workers_session = NULL;
}
