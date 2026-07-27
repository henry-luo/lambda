// node_tty.cpp — node:tty compatibility facade through opaque Jube services.
#include "node_tty.hpp"
#include "../../jube/jube_registry.h"

#include <cstring>

static const JubeHostAPI* node_tty_host = NULL;
struct NodeTtySessionState {
    void* session;
    bool rooted;
    Item cached_namespace;
};
static NodeTtySessionState* node_tty_state(void) {
    return (NodeTtySessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_TTY);
}
#define node_tty_session (node_tty_state()->session)
#define node_tty_rooted (node_tty_state()->rooted)
#define node_tty_cached_namespace (node_tty_state()->cached_namespace)

static Item node_tty_undefined(Item unused) {
    (void)unused;
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_tty_empty_object(Item unused) {
    (void)unused;
    return node_tty_host->value->new_object();
}

static Item node_tty_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static void node_tty_set_property(Item object, const char* name, Item value) {
    JubeRootFrame frame = {};
    if (!node_tty_host->node->roots->root_frame_begin(&frame, 3)) return;
    uint64_t* object_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_tty_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    *value_root = value.item;
    Item key = node_tty_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    // Key creation can compact before the receiver owns the new property.
    node_tty_host->value->property_set(node_tty_root_value(object_root),
        node_tty_root_value(key_root), node_tty_root_value(value_root));
    node_tty_host->node->roots->root_frame_end(&frame);
}

static Item node_tty_property(Item object, const char* name) {
    JubeRootFrame frame = {};
    if (!node_tty_host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* key_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root) {
        node_tty_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item key = node_tty_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item result = node_tty_host->value->property_get(object, node_tty_root_value(key_root));
    node_tty_host->node->roots->root_frame_end(&frame);
    return result;
}

static void node_tty_set_method(Item object, const char* name, void* function,
                                int parameter_count) {
    JubeRootFrame frame = {};
    if (!node_tty_host->node->roots->root_frame_begin(&frame, 2)) return;
    uint64_t* object_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !function_root) {
        node_tty_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    Item method = node_tty_host->script->new_function(function, parameter_count);
    *function_root = method.item;
    node_tty_set_property(node_tty_root_value(object_root), name,
        node_tty_root_value(function_root));
    node_tty_host->node->roots->root_frame_end(&frame);
}

static void node_tty_build_stream_constructor(Item namespace_item, const char* name,
                                              Item socket_fn, Item socket_proto) {
    JubeRootFrame frame = {};
    if (!node_tty_host->node->roots->root_frame_begin(&frame, 5)) return;
    uint64_t* namespace_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* prototype_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* socket_function_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* socket_prototype_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !function_root || !prototype_root || !socket_function_root ||
            !socket_prototype_root) {
        node_tty_host->node->roots->root_frame_end(&frame);
        return;
    }
    *namespace_root = namespace_item.item;
    *socket_function_root = socket_fn.item;
    *socket_prototype_root = socket_proto.item;
    Item constructor = node_tty_host->script->new_function((void*)node_tty_empty_object, 1);
    *function_root = constructor.item;
    Item prototype = node_tty_host->value->new_object();
    *prototype_root = prototype.item;
    node_tty_set_property(node_tty_root_value(prototype_root), "constructor",
        node_tty_root_value(function_root));
    Item constructor_key = node_tty_host->value->string_from_utf8_n("constructor", 11);
    node_tty_host->script->mark_non_enumerable(node_tty_root_value(prototype_root), constructor_key);
    node_tty_host->script->function_set_prototype(node_tty_root_value(function_root),
        node_tty_root_value(prototype_root));
    node_tty_host->script->set_prototype(node_tty_root_value(function_root),
        node_tty_root_value(socket_function_root));
    // Constructor/prototype creation may compact before the inherited Socket
    // prototype is attached, so retain the host value in this frame.
    node_tty_host->script->set_prototype(node_tty_root_value(prototype_root),
        node_tty_root_value(socket_prototype_root));
    node_tty_set_property(node_tty_root_value(namespace_root), name,
        node_tty_root_value(function_root));
    node_tty_host->node->roots->root_frame_end(&frame);
}

Item node_tty_namespace(void) {
    if (node_tty_cached_namespace.item != 0) return node_tty_cached_namespace;
    if (!node_tty_host || !node_tty_session) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_tty_host->node->roots->root_frame_begin(&frame, 4)) return ItemNull;
    uint64_t* namespace_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* net_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* socket_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* socket_prototype_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !net_root || !socket_root || !socket_prototype_root) {
        node_tty_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    node_tty_cached_namespace = node_tty_host->value->new_object();
    *namespace_root = node_tty_cached_namespace.item;
    node_tty_set_method(node_tty_root_value(namespace_root), "isatty", (void*)node_tty_undefined, 1);
    Item net_namespace = ItemNull;
    if (node_tty_host->node->runtime->resolve_host_namespace(node_tty_session, "net",
            &net_namespace) == 0) {
        *net_root = net_namespace.item;
        Item socket = node_tty_property(node_tty_root_value(net_root), "Socket");
        *socket_root = socket.item;
        Item socket_prototype = node_tty_property(node_tty_root_value(socket_root), "prototype");
        *socket_prototype_root = socket_prototype.item;
        node_tty_build_stream_constructor(node_tty_root_value(namespace_root), "WriteStream",
            node_tty_root_value(socket_root), node_tty_root_value(socket_prototype_root));
        node_tty_build_stream_constructor(node_tty_root_value(namespace_root), "ReadStream",
            node_tty_root_value(socket_root), node_tty_root_value(socket_prototype_root));
    } else {
        node_tty_set_method(node_tty_root_value(namespace_root), "WriteStream",
            (void*)node_tty_empty_object, 1);
        node_tty_set_method(node_tty_root_value(namespace_root), "ReadStream",
            (void*)node_tty_empty_object, 1);
    }
    node_tty_set_property(node_tty_root_value(namespace_root), "default",
        node_tty_root_value(namespace_root));
    node_tty_cached_namespace = node_tty_root_value(namespace_root);
    node_tty_host->node->roots->root_frame_end(&frame);
    return node_tty_cached_namespace;
}

int node_tty_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots || !host->value ||
            !host->script || !host->node->runtime->resolve_host_namespace ||
            !host->value->new_object || !host->value->property_get || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->script->new_function ||
            !host->script->function_set_prototype || !host->script->set_prototype ||
            !host->script->mark_non_enumerable) return -1;
    node_tty_host = host;
    return 0;
}

void node_tty_shutdown(void) {
    node_tty_host = NULL;
}

void node_tty_runtime_attach(void* session) {
    if (!node_tty_host || !node_tty_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_TTY,
            sizeof(NodeTtySessionState))) return;
    node_tty_session = session;
    if (node_tty_host->node->roots->persistent_root_register(session,
            &node_tty_cached_namespace.item) == 0) node_tty_rooted = true;
}

void node_tty_runtime_reset(void* session) {
    if (session == node_tty_session) node_tty_cached_namespace = (Item){0};
}

void node_tty_runtime_detach(void* session) {
    if (!node_tty_host || session != node_tty_session) return;
    if (node_tty_rooted) node_tty_host->node->roots->persistent_root_unregister(
        session, &node_tty_cached_namespace.item);
    node_tty_rooted = false;
    node_tty_cached_namespace = (Item){0};
    node_tty_session = NULL;
}
