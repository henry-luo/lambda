/**
 * node_punycode.cpp — Node.js compatibility 'punycode' stub through Jube
 *
 * The current compatibility surface intentionally exposes only the legacy
 * namespace shape.  Its no-op transforms match the prior host stub while the
 * namespace cache remains owned by the active JS session.
 */
#include "node_punycode.hpp"
#include "../../jube/jube_registry.h"

#include <cstring>

static const JubeHostAPI* node_punycode_host = NULL;
struct NodePunycodeSessionState { void* session; bool rooted; Item cached_namespace; };
static NodePunycodeSessionState* node_punycode_state(void) { return (NodePunycodeSessionState*)jube_node_current_module_state(JUBE_NODE_MODULE_STATE_PUNYCODE); }
#define node_punycode_session (node_punycode_state()->session)
#define node_punycode_rooted (node_punycode_state()->rooted)
#define node_punycode_cached_namespace (node_punycode_state()->cached_namespace)

static Item node_punycode_undefined(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static Item node_punycode_noop(Item value) {
    (void)value;
    return node_punycode_undefined();
}

static bool node_punycode_roots_begin(JubeRootFrame* frame, size_t count) {
    return node_punycode_host && node_punycode_host->node && node_punycode_host->node->roots &&
        node_punycode_host->node->roots->root_frame_begin &&
        node_punycode_host->node->roots->root_frame_take_slot &&
        node_punycode_host->node->roots->root_frame_end &&
        node_punycode_host->node->roots->root_frame_begin(frame, count);
}

static Item node_punycode_string(const char* text) {
    return node_punycode_host->value->string_from_utf8_n(text, strlen(text));
}

static Item node_punycode_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static void node_punycode_set_property(Item value, const char* name, Item property) {
    JubeRootFrame frame = {};
    if (!node_punycode_roots_begin(&frame, 3)) return;
    uint64_t* namespace_root = node_punycode_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_punycode_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* property_root = node_punycode_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root || !property_root) {
        node_punycode_host->node->roots->root_frame_end(&frame);
        return;
    }
    *namespace_root = value.item;
    *property_root = property.item;
    Item key = node_punycode_string(name);
    *key_root = key.item;
    // String/function creation can compact the namespace, so reload it from
    // the root slot before publishing the property.
    value = node_punycode_root_value(namespace_root);
    property = node_punycode_root_value(property_root);
    node_punycode_host->value->property_set(value, key, property);
    node_punycode_host->node->roots->root_frame_end(&frame);
}

static void node_punycode_set_method(Item value, const char* name, void* function_ptr) {
    JubeRootFrame frame = {};
    if (!node_punycode_roots_begin(&frame, 2)) return;
    uint64_t* namespace_root = node_punycode_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_punycode_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !function_root) {
        node_punycode_host->node->roots->root_frame_end(&frame);
        return;
    }
    *namespace_root = value.item;
    Item function = node_punycode_host->script->new_function(function_ptr, 1);
    *function_root = function.item;
    value = node_punycode_root_value(namespace_root);
    function = node_punycode_root_value(function_root);
    node_punycode_set_property(value, name, function);
    node_punycode_host->node->roots->root_frame_end(&frame);
}

Item node_punycode_namespace(void) {
    if (node_punycode_cached_namespace.item != 0) return node_punycode_cached_namespace;
    if (!node_punycode_host || !node_punycode_session) return ItemNull;

    node_punycode_cached_namespace = node_punycode_host->value->new_object();
    node_punycode_set_method(node_punycode_cached_namespace, "encode", (void*)node_punycode_noop);
    node_punycode_set_method(node_punycode_cached_namespace, "decode", (void*)node_punycode_noop);
    node_punycode_set_method(node_punycode_cached_namespace, "toASCII", (void*)node_punycode_noop);
    node_punycode_set_method(node_punycode_cached_namespace, "toUnicode", (void*)node_punycode_noop);
    node_punycode_set_property(node_punycode_cached_namespace, "version",
                               node_punycode_string("2.3.1"));
    node_punycode_set_property(node_punycode_cached_namespace, "ucs2",
                               node_punycode_host->value->new_object());
    node_punycode_set_property(node_punycode_cached_namespace, "default",
                               node_punycode_cached_namespace);
    return node_punycode_cached_namespace;
}

static void node_punycode_cache_reset(void) {
    node_punycode_cached_namespace = (Item){0};
}

int node_punycode_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots || !host->value ||
            !host->script || !host->value->new_object || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->script->new_function) return -1;
    node_punycode_host = host;
    return 0;
}

void node_punycode_shutdown(void) {
    node_punycode_host = NULL;
}

void node_punycode_runtime_attach(void* session) {
    if (!node_punycode_host || !node_punycode_host->node->runtime->session_is_live ||
            !node_punycode_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_PUNYCODE,
            sizeof(NodePunycodeSessionState))) return;
    node_punycode_session = session;
    if (node_punycode_host->node->roots->persistent_root_register(session,
            &node_punycode_cached_namespace.item) == 0) {
        node_punycode_rooted = true;
    }
}

void node_punycode_runtime_reset(void* session) {
    if (session == node_punycode_session) node_punycode_cache_reset();
}

void node_punycode_runtime_detach(void* session) {
    if (!node_punycode_host || session != node_punycode_session) return;
    if (node_punycode_rooted) {
        node_punycode_host->node->roots->persistent_root_unregister(session,
            &node_punycode_cached_namespace.item);
        node_punycode_rooted = false;
    }
    node_punycode_cache_reset();
    node_punycode_session = NULL;
}
