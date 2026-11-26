// node_tty.cpp — node:tty compatibility facade through opaque Jube services.
#include "node_tty.hpp"
#include "../../jube/jube_registry.h"

#include <cstring>

static const JubeHostAPI* node_tty_host = NULL;
struct NodeTtySessionState {
    JubePersistentValueSlots cache_values;
    Item cache_items[1];
};
static NodeTtySessionState* node_tty_state(void) {
    return (NodeTtySessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_TTY);
}
#define node_tty_session (node_tty_state()->cache_values.session)
#define node_tty_cached_namespace (node_tty_state()->cache_items[0])

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

template <typename Target>
static void node_tty_set_method(Item object, const char* name, Target target,
                                int adapter_arity) {
    JubeRootFrame frame = {};
    if (!node_tty_host->node->roots->root_frame_begin(&frame, 2)) return;
    uint64_t* object_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !function_root) {
        node_tty_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    Item method = jube_new_function(node_tty_host->script, target,
        adapter_arity);
    *function_root = method.item;
    node_tty_set_property(node_tty_root_value(object_root), name,
        node_tty_root_value(function_root));
    node_tty_host->node->roots->root_frame_end(&frame);
}

Item node_tty_namespace(void) {
    if (node_tty_cached_namespace.item != 0) return node_tty_cached_namespace;
    if (!node_tty_host || !node_tty_session) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_tty_host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* namespace_root = node_tty_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root) {
        node_tty_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    node_tty_cached_namespace = node_tty_host->value->new_object();
    *namespace_root = node_tty_cached_namespace.item;
    node_tty_set_method(node_tty_root_value(namespace_root), "isatty", node_tty_undefined, 1);
    // net is not in the shipped Node module set (D9.4.1), so these stay inert.
    node_tty_set_method(node_tty_root_value(namespace_root), "WriteStream",
        node_tty_empty_object, 1);
    node_tty_set_method(node_tty_root_value(namespace_root), "ReadStream",
        node_tty_empty_object, 1);
    node_tty_set_property(node_tty_root_value(namespace_root), "default",
        node_tty_root_value(namespace_root));
    node_tty_cached_namespace = node_tty_root_value(namespace_root);
    node_tty_host->node->roots->root_frame_end(&frame);
    return node_tty_cached_namespace;
}

int node_tty_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->roots || !host->value || !host->script ||
            !host->value->new_object || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->script->new_function) return -1;
    node_tty_host = host;
    return 0;
}

void node_tty_shutdown(void) {
    node_tty_host = NULL;
}

void node_tty_runtime_attach(void* session) {
    if (!node_tty_host || !node_tty_host->node->runtime->session_is_live(session)) return;
    NodeTtySessionState* state = (NodeTtySessionState*)
        jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_TTY,
            sizeof(NodeTtySessionState));
    if (!state) return;
    jube_persistent_value_slots_attach(&state->cache_values, session,
        node_tty_host->node->roots, state->cache_items, 1);
}

void node_tty_runtime_reset(void* session) {
    if (session == node_tty_session) {
        jube_persistent_value_slots_reset(&node_tty_state()->cache_values);
    }
}

void node_tty_runtime_detach(void* session) {
    NodeTtySessionState* state = node_tty_state();
    if (!node_tty_host || !state || session != state->cache_values.session) return;
    jube_persistent_value_slots_detach(&state->cache_values);
}
