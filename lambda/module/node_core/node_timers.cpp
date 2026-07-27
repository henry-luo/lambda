// node_timers.cpp — node:timers/promises facade through Jube timer services.
#include "node_timers.hpp"
#include "../../jube/jube_registry.h"

#include <cstring>

static const JubeHostAPI* node_timers_host = NULL;
struct NodeTimersSessionState { void* session; bool rooted; bool classic_rooted; Item cached_namespace; Item classic_namespace; };
static NodeTimersSessionState* node_timers_state(void) { return (NodeTimersSessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_TIMERS); }
#define node_timers_session (node_timers_state()->session)
#define node_timers_rooted (node_timers_state()->rooted)
#define node_timers_classic_rooted (node_timers_state()->classic_rooted)
#define node_timers_cached_namespace (node_timers_state()->cached_namespace)
#define node_timers_classic_namespace (node_timers_state()->classic_namespace)

static Item node_timers_timeout(Item delay, Item value, Item options) {
    return node_timers_host->node->async_ops->timer_set_timeout_promise(delay, value, options);
}

static Item node_timers_immediate(Item value, Item options) {
    return node_timers_host->node->async_ops->timer_set_immediate_promise(value, options);
}

static Item node_timers_interval(Item callback, Item delay) {
    return node_timers_host->node->async_ops->timer_set_interval(callback, delay);
}

static Item node_timers_scheduler_wait(Item delay, Item options) {
    return node_timers_host->node->async_ops->timer_scheduler_wait(delay, options);
}

static Item node_timers_scheduler_yield(void) {
    return node_timers_host->node->async_ops->timer_scheduler_yield();
}

static Item node_timers_timeout_callback(Item callback, Item delay) {
    return node_timers_host->node->async_ops->timer_set_timeout(callback, delay);
}

static Item node_timers_interval_callback(Item callback, Item delay) {
    return node_timers_host->node->async_ops->timer_set_interval(callback, delay);
}

static Item node_timers_undefined(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_timers_clear_timeout(Item timer) {
    node_timers_host->node->async_ops->timer_clear_timeout(timer);
    return node_timers_undefined();
}

static Item node_timers_clear_interval(Item timer) {
    node_timers_host->node->async_ops->timer_clear_interval(timer);
    return node_timers_undefined();
}

static Item node_timers_immediate_callback(Item callback) {
    return node_timers_host->node->async_ops->timer_set_immediate(callback);
}

static Item node_timers_from_root(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static void node_timers_set_property(Item object, const char* name, Item value) {
    JubeRootFrame frame = {};
    if (!node_timers_host->node->roots->root_frame_begin(&frame, 3)) return;
    uint64_t* object_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_timers_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    *value_root = value.item;
    Item key = node_timers_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    // Key/function creation can compact the heap, so publish rooted values.
    object = node_timers_from_root(object_root);
    value = node_timers_from_root(value_root);
    key = node_timers_from_root(key_root);
    node_timers_host->value->property_set(object, key, value);
    node_timers_host->node->roots->root_frame_end(&frame);
}

static void node_timers_set_method(Item object, const char* name, void* function, int parameter_count) {
    JubeRootFrame frame = {};
    if (!node_timers_host->node->roots->root_frame_begin(&frame, 2)) return;
    uint64_t* object_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !function_root) {
        node_timers_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    Item method = node_timers_host->script->new_function(function, parameter_count);
    *function_root = method.item;
    object = node_timers_from_root(object_root);
    method = node_timers_from_root(function_root);
    node_timers_set_property(object, name, method);
    node_timers_host->node->roots->root_frame_end(&frame);
}

Item node_timers_promises_namespace(void) {
    if (node_timers_cached_namespace.item != 0) return node_timers_cached_namespace;
    if (!node_timers_host || !node_timers_session) return ItemNull;

    node_timers_cached_namespace = node_timers_host->value->new_object();
    node_timers_set_method(node_timers_cached_namespace, "setTimeout", (void*)node_timers_timeout, 3);
    node_timers_set_method(node_timers_cached_namespace, "setInterval", (void*)node_timers_interval, 2);
    node_timers_set_method(node_timers_cached_namespace, "setImmediate", (void*)node_timers_immediate, 2);
    JubeRootFrame frame = {};
    if (!node_timers_host->node->roots->root_frame_begin(&frame, 1)) return ItemNull;
    uint64_t* scheduler_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
    if (!scheduler_root) {
        node_timers_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item scheduler = node_timers_host->value->new_object();
    *scheduler_root = scheduler.item;
    node_timers_set_method(scheduler, "wait", (void*)node_timers_scheduler_wait, 2);
    scheduler = node_timers_from_root(scheduler_root);
    node_timers_set_method(scheduler, "yield", (void*)node_timers_scheduler_yield, 0);
    scheduler = node_timers_from_root(scheduler_root);
    // Method creation may compact the scheduler before it is attached to the
    // persistent namespace; retain it in the temporary root until publication.
    node_timers_set_property(node_timers_cached_namespace, "scheduler", scheduler);
    node_timers_host->node->roots->root_frame_end(&frame);
    node_timers_set_property(node_timers_cached_namespace, "default", node_timers_cached_namespace);
    return node_timers_cached_namespace;
}

Item node_timers_namespace(void) {
    if (node_timers_classic_namespace.item != 0) return node_timers_classic_namespace;
    if (!node_timers_host || !node_timers_session) return ItemNull;

    node_timers_classic_namespace = node_timers_host->value->new_object();
    node_timers_set_method(node_timers_classic_namespace, "setTimeout",
                           (void*)node_timers_timeout_callback, 2);
    node_timers_set_method(node_timers_classic_namespace, "setInterval",
                           (void*)node_timers_interval_callback, 2);
    node_timers_set_method(node_timers_classic_namespace, "clearTimeout",
                           (void*)node_timers_clear_timeout, 1);
    node_timers_set_method(node_timers_classic_namespace, "clearInterval",
                           (void*)node_timers_clear_interval, 1);
    node_timers_set_method(node_timers_classic_namespace, "setImmediate",
                           (void*)node_timers_immediate_callback, 1);
    JubeRootFrame frame = {};
    if (node_timers_host->node->roots->root_frame_begin(&frame, 3)) {
        uint64_t* namespace_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
        uint64_t* key_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
        uint64_t* function_root = node_timers_host->node->roots->root_frame_take_slot(&frame);
        if (namespace_root && key_root && function_root) {
            *namespace_root = node_timers_classic_namespace.item;
            Item key = node_timers_host->value->string_from_utf8_n("setTimeout", 10);
            *key_root = key.item;
            Item timeout = node_timers_host->value->property_get(
                node_timers_from_root(namespace_root), node_timers_from_root(key_root));
            *function_root = timeout.item;
            // Installing Symbol.for('nodejs.util.promisify.custom') may allocate;
            // hold the timeout export until its persistent namespace owns it.
            node_timers_host->node->async_ops->timer_install_promisify_custom(
                node_timers_from_root(function_root));
        }
        node_timers_host->node->roots->root_frame_end(&frame);
    }
    node_timers_set_property(node_timers_classic_namespace, "default",
                             node_timers_classic_namespace);
    node_timers_set_property(node_timers_classic_namespace, "promises",
                             node_timers_promises_namespace());
    return node_timers_classic_namespace;
}

int node_timers_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->roots || !host->node->async_ops ||
            host->node->async_ops->struct_size < sizeof(JubeHostAsyncAPI) || !host->value ||
            !host->value->new_object || !host->value->property_get || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->script || !host->script->new_function ||
            !host->node->async_ops->timer_set_timeout_promise ||
            !host->node->async_ops->timer_set_immediate_promise ||
            !host->node->async_ops->timer_set_interval ||
            !host->node->async_ops->timer_scheduler_wait ||
            !host->node->async_ops->timer_scheduler_yield ||
            !host->node->async_ops->timer_set_timeout ||
            !host->node->async_ops->timer_clear_timeout ||
            !host->node->async_ops->timer_clear_interval ||
            !host->node->async_ops->timer_set_immediate ||
            !host->node->async_ops->timer_install_promisify_custom) {
        return -1;
    }
    node_timers_host = host;
    return 0;
}

void node_timers_shutdown(void) {
    node_timers_host = NULL;
}

void node_timers_runtime_attach(void* session) {
    if (!node_timers_host || !node_timers_host->node->runtime->session_is_live ||
            !node_timers_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_TIMERS,
            sizeof(NodeTimersSessionState))) return;
    node_timers_session = session;
    if (node_timers_host->node->roots->persistent_root_register(session,
            &node_timers_cached_namespace.item) == 0) node_timers_rooted = true;
    if (node_timers_host->node->roots->persistent_root_register(session,
            &node_timers_classic_namespace.item) == 0) node_timers_classic_rooted = true;
}

void node_timers_runtime_reset(void* session) {
    if (session == node_timers_session) {
        node_timers_cached_namespace = (Item){0};
        node_timers_classic_namespace = (Item){0};
    }
}

void node_timers_runtime_detach(void* session) {
    if (!node_timers_host || session != node_timers_session) return;
    if (node_timers_rooted) {
        node_timers_host->node->roots->persistent_root_unregister(session,
            &node_timers_cached_namespace.item);
        node_timers_rooted = false;
    }
    if (node_timers_classic_rooted) {
        node_timers_host->node->roots->persistent_root_unregister(session,
            &node_timers_classic_namespace.item);
        node_timers_classic_rooted = false;
    }
    node_timers_cached_namespace = (Item){0};
    node_timers_classic_namespace = (Item){0};
    node_timers_session = NULL;
}
