// node_v8.cpp — bounded node:v8 compatibility stubs through Jube.
#include "node_v8.hpp"

#include <cstring>

static const JubeHostAPI* node_v8_host = NULL;
static void* node_v8_session = NULL;
static bool node_v8_rooted = false;
static Item node_v8_cached_namespace = {0};

static Item node_v8_undefined(Item value) {
    (void)value;
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_v8_new_object(Item value) {
    (void)value;
    return node_v8_host->value->new_object();
}

static Item node_v8_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static void node_v8_set_property(Item object, const char* name, Item value) {
    JubeRootFrame frame = {};
    if (!node_v8_host->node->roots->root_frame_begin(&frame, 3)) return;
    uint64_t* object_root = node_v8_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_v8_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_v8_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_v8_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    *value_root = value.item;
    Item key = node_v8_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    // Name allocation may compact the namespace before the property write.
    node_v8_host->value->property_set(node_v8_root_value(object_root),
        node_v8_root_value(key_root), node_v8_root_value(value_root));
    node_v8_host->node->roots->root_frame_end(&frame);
}

static void node_v8_set_method(Item object, const char* name, void* function) {
    JubeRootFrame frame = {};
    if (!node_v8_host->node->roots->root_frame_begin(&frame, 2)) return;
    uint64_t* object_root = node_v8_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_v8_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !function_root) {
        node_v8_host->node->roots->root_frame_end(&frame);
        return;
    }
    *object_root = object.item;
    Item method = node_v8_host->script->new_function(function, 1);
    *function_root = method.item;
    node_v8_set_property(node_v8_root_value(object_root), name, node_v8_root_value(function_root));
    node_v8_host->node->roots->root_frame_end(&frame);
}

Item node_v8_namespace(void) {
    if (node_v8_cached_namespace.item != 0) return node_v8_cached_namespace;
    if (!node_v8_host || !node_v8_session) return ItemNull;
    node_v8_cached_namespace = node_v8_host->value->new_object();
    JubeRootFrame frame = {};
    if (!node_v8_host->node->roots->root_frame_begin(&frame, 2)) {
        node_v8_cached_namespace = (Item){0};
        return ItemNull;
    }
    uint64_t* promise_hooks_root = node_v8_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* startup_snapshot_root = node_v8_host->node->roots->root_frame_take_slot(&frame);
    if (!promise_hooks_root || !startup_snapshot_root) {
        node_v8_host->node->roots->root_frame_end(&frame);
        node_v8_cached_namespace = (Item){0};
        return ItemNull;
    }
    Item promise_hooks = node_v8_host->value->new_object();
    *promise_hooks_root = promise_hooks.item;
    // Method creation can compact the heap, so reload the temporary child
    // through its root before every call that receives it by value.
    node_v8_set_method(node_v8_root_value(promise_hooks_root), "onInit", (void*)node_v8_undefined);
    node_v8_set_method(node_v8_root_value(promise_hooks_root), "onBefore", (void*)node_v8_undefined);
    node_v8_set_method(node_v8_root_value(promise_hooks_root), "onAfter", (void*)node_v8_undefined);
    node_v8_set_method(node_v8_root_value(promise_hooks_root), "onSettled", (void*)node_v8_undefined);
    node_v8_set_method(node_v8_root_value(promise_hooks_root), "createHook", (void*)node_v8_undefined);
    node_v8_set_property(node_v8_cached_namespace, "promiseHooks", node_v8_root_value(promise_hooks_root));
    node_v8_set_method(node_v8_cached_namespace, "getHeapStatistics", (void*)node_v8_new_object);
    node_v8_set_method(node_v8_cached_namespace, "getHeapSpaceStatistics", (void*)node_v8_new_object);
    node_v8_set_method(node_v8_cached_namespace, "setFlagsFromString", (void*)node_v8_undefined);
    node_v8_set_method(node_v8_cached_namespace, "serialize", (void*)node_v8_undefined);
    node_v8_set_method(node_v8_cached_namespace, "deserialize", (void*)node_v8_undefined);
    Item startup_snapshot = node_v8_host->value->new_object();
    *startup_snapshot_root = startup_snapshot.item;
    node_v8_set_method(node_v8_root_value(startup_snapshot_root),
        "setDeserializeMainFunction", (void*)node_v8_undefined);
    node_v8_set_property(node_v8_cached_namespace, "startupSnapshot",
        node_v8_root_value(startup_snapshot_root));
    node_v8_set_method(node_v8_cached_namespace, "GCProfiler", (void*)node_v8_new_object);
    node_v8_set_property(node_v8_cached_namespace, "default", node_v8_cached_namespace);
    node_v8_host->node->roots->root_frame_end(&frame);
    return node_v8_cached_namespace;
}

int node_v8_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->roots || !host->value || !host->script ||
            !host->value->new_object || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->script->new_function) return -1;
    node_v8_host = host;
    return 0;
}

void node_v8_shutdown(void) {
    node_v8_cached_namespace = (Item){0};
    node_v8_rooted = false;
    node_v8_session = NULL;
    node_v8_host = NULL;
}

void node_v8_runtime_attach(void* session) {
    if (!node_v8_host || !node_v8_host->node->runtime->session_is_live(session)) return;
    node_v8_session = session;
    if (node_v8_host->node->roots->persistent_root_register(
            session, &node_v8_cached_namespace.item) == 0) {
        node_v8_rooted = true;
    }
}

void node_v8_runtime_reset(void* session) {
    if (session == node_v8_session) node_v8_cached_namespace = (Item){0};
}

void node_v8_runtime_detach(void* session) {
    if (!node_v8_host || session != node_v8_session) return;
    if (node_v8_rooted) {
        node_v8_host->node->roots->persistent_root_unregister(
            session, &node_v8_cached_namespace.item);
    }
    node_v8_rooted = false;
    node_v8_cached_namespace = (Item){0};
    node_v8_session = NULL;
}
