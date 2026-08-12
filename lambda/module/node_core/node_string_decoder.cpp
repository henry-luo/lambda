/**
 * node_string_decoder.cpp — Jube-hosted Node.js 'string_decoder' namespace.
 *
 * The decoder operates on opaque host binary values and never reaches into
 * LambdaJS typed-array, object, or receiver storage.
 */
#include "node_string_decoder.hpp"
#include "../../jube/jube_registry.h"

#include <cstring>

static const JubeHostAPI* node_string_decoder_host = NULL;
struct NodeStringDecoderSessionState {
    void* session;
    bool rooted;
    Item namespace_cache;
};
static NodeStringDecoderSessionState* node_string_decoder_state(void) {
    return (NodeStringDecoderSessionState*)jube_node_current_module_state(
        JUBE_NODE_MODULE_STATE_STRING_DECODER);
}
#define node_string_decoder_session (node_string_decoder_state()->session)
#define node_string_decoder_rooted (node_string_decoder_state()->rooted)
#define node_string_decoder_namespace_cache (node_string_decoder_state()->namespace_cache)

extern "C" Item node_string_decoder_write(Item buffer);
extern "C" Item node_string_decoder_end(Item buffer);

static int node_string_decoder_kind(Item value) {
    return node_string_decoder_host && node_string_decoder_host->value &&
            node_string_decoder_host->value->kind ?
        node_string_decoder_host->value->kind(value) : JUBE_VALUE_OTHER;
}

static Item node_string_decoder_string(const char* text, int length) {
    if (!node_string_decoder_host || !node_string_decoder_host->value ||
            !node_string_decoder_host->value->string_from_utf8_n || !text || length < 0) {
        return ItemNull;
    }
    return node_string_decoder_host->value->string_from_utf8_n(text, (size_t)length);
}

static Item node_string_decoder_string(const char* text) {
    return node_string_decoder_string(text, text ? (int)strlen(text) : 0);
}

static bool node_string_decoder_roots_begin(JubeRootFrame* frame, int slot_count) {
    return node_string_decoder_host && node_string_decoder_host->node &&
            node_string_decoder_host->node->roots &&
            node_string_decoder_host->node->roots->root_frame_begin &&
            node_string_decoder_host->node->roots->root_frame_take_slot &&
            node_string_decoder_host->node->roots->root_frame_end &&
            slot_count > 0 &&
            node_string_decoder_host->node->roots->root_frame_begin(frame, (size_t)slot_count);
}

static void node_string_decoder_roots_end(JubeRootFrame* frame) {
    node_string_decoder_host->node->roots->root_frame_end(frame);
}

static void node_string_decoder_set_string_property(Item object, const char* name,
                                                     const char* value) {
    JubeRootFrame frame = {};
    if (!node_string_decoder_roots_begin(&frame, 2)) return;
    uint64_t* key_root = node_string_decoder_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_string_decoder_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root || !value_root) {
        node_string_decoder_roots_end(&frame);
        return;
    }
    Item key = node_string_decoder_string(name);
    *key_root = key.item;
    Item item_value = node_string_decoder_string(value);
    *value_root = item_value.item;
    node_string_decoder_host->value->property_set(object, key, item_value);
    node_string_decoder_roots_end(&frame);
}

static void node_string_decoder_set_item_property(Item object, const char* name, Item value) {
    JubeRootFrame frame = {};
    if (!node_string_decoder_roots_begin(&frame, 2)) return;
    uint64_t* key_root = node_string_decoder_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_string_decoder_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root || !value_root) {
        node_string_decoder_roots_end(&frame);
        return;
    }
    *value_root = value.item;
    Item key = node_string_decoder_string(name);
    *key_root = key.item;
    node_string_decoder_host->value->property_set(object, key, value);
    node_string_decoder_roots_end(&frame);
}

static uint8_t* node_string_decoder_buffer_data(Item buffer, int* out_length) {
    if (out_length) *out_length = 0;
    if (!node_string_decoder_host || !node_string_decoder_host->node ||
            !node_string_decoder_host->node->binary ||
            !node_string_decoder_host->node->binary->is_typed_array ||
            !node_string_decoder_host->node->binary->typed_array_data ||
            !node_string_decoder_host->node->binary->typed_array_length ||
            !node_string_decoder_host->node->binary->is_typed_array(buffer)) {
        return NULL;
    }
    uint8_t* data = node_string_decoder_host->node->binary->typed_array_data(buffer);
    if (!data) return NULL;
    if (out_length) *out_length = node_string_decoder_host->node->binary->typed_array_length(buffer);
    return data;
}

template <typename Target>
static void node_string_decoder_set_method(Item object, const char* name,
        Target target, int adapter_arity) {
    if (!node_string_decoder_host || !node_string_decoder_host->value ||
            !node_string_decoder_host->script || !node_string_decoder_host->value->property_set ||
            !node_string_decoder_host->script->new_function) return;
    JubeRootFrame frame = {};
    if (!node_string_decoder_roots_begin(&frame, 2)) return;
    uint64_t* key_root = node_string_decoder_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* method_root = node_string_decoder_host->node->roots->root_frame_take_slot(&frame);
    if (!key_root || !method_root) {
        node_string_decoder_roots_end(&frame);
        return;
    }
    Item key = node_string_decoder_string(name);
    *key_root = key.item;
    Item method = jube_new_function(node_string_decoder_host->script, target,
        adapter_arity);
    *method_root = method.item;
    node_string_decoder_host->value->property_set(object, key, method);
    node_string_decoder_roots_end(&frame);
}

// How many bytes does the leading byte of a UTF-8 sequence indicate?
static int node_string_decoder_utf8_char_len(uint8_t byte) {
    if ((byte & 0x80) == 0) return 1;
    if ((byte & 0xE0) == 0xC0) return 2;
    if ((byte & 0xF0) == 0xE0) return 3;
    if ((byte & 0xF8) == 0xF0) return 4;
    return 1;
}

// new StringDecoder([encoding])
extern "C" Item node_string_decoder_new(Item encoding_item) {
    if (!node_string_decoder_host || !node_string_decoder_host->value ||
            !node_string_decoder_host->value->new_object ||
            !node_string_decoder_host->value->property_set) return ItemNull;

    JubeRootFrame frame = {};
    if (!node_string_decoder_roots_begin(&frame, 1)) return ItemNull;
    uint64_t* decoder_root = node_string_decoder_host->node->roots->root_frame_take_slot(&frame);
    if (!decoder_root) {
        node_string_decoder_roots_end(&frame);
        return ItemNull;
    }
    Item decoder = node_string_decoder_host->value->new_object();
    *decoder_root = decoder.item;
    char encoding[32] = "utf8";
    if (node_string_decoder_kind(encoding_item) == JUBE_VALUE_STRING &&
            node_string_decoder_host->value->string_copy) {
        // The local copy keeps the guest independent of the host String layout.
        node_string_decoder_host->value->string_copy(encoding_item, encoding, sizeof(encoding), NULL);
    }
    node_string_decoder_set_string_property(decoder, "encoding", encoding);
    node_string_decoder_set_string_property(decoder, "__pending__", "");
    node_string_decoder_set_item_property(decoder, "__pending_len__", (Item){.item = i2it(0)});
    node_string_decoder_set_method(decoder, "write", node_string_decoder_write, 1);
    node_string_decoder_set_method(decoder, "end", node_string_decoder_end, 1);
    node_string_decoder_roots_end(&frame);
    return decoder;
}

// decoder.write(buffer) — decode buffer, returning complete UTF-8 bytes.
extern "C" Item node_string_decoder_write(Item buffer) {
    int length = 0;
    uint8_t* data = node_string_decoder_buffer_data(buffer, &length);
    if (!data || length <= 0) return node_string_decoder_string("", 0);

    int complete_end = length;
    int index = length - 1;
    while (index > 0 && (data[index] & 0xC0) == 0x80) index--;
    int expected = node_string_decoder_utf8_char_len(data[index]);
    if (length - index < expected) {
        // Do not expose an incomplete code point at a chunk boundary.
        complete_end = index;
    }
    return node_string_decoder_string((const char*)data, complete_end);
}

// decoder.end([buffer]) — flush the supplied final chunk.
extern "C" Item node_string_decoder_end(Item buffer) {
    if (node_string_decoder_kind(buffer) != JUBE_VALUE_UNDEFINED &&
            node_string_decoder_kind(buffer) != JUBE_VALUE_NULL) {
        return node_string_decoder_write(buffer);
    }
    return node_string_decoder_string("", 0);
}

Item node_string_decoder_namespace(void) {
    if (node_string_decoder_namespace_cache.item != 0) return node_string_decoder_namespace_cache;
    if (!node_string_decoder_host || !node_string_decoder_session ||
            !node_string_decoder_host->value || !node_string_decoder_host->value->new_object ||
            !node_string_decoder_host->value->property_set) return ItemNull;

    node_string_decoder_namespace_cache = node_string_decoder_host->value->new_object();
    node_string_decoder_set_method(node_string_decoder_namespace_cache, "StringDecoder",
                                   node_string_decoder_new, 1);
    node_string_decoder_set_method(node_string_decoder_namespace_cache, "write",
                                   node_string_decoder_write, 1);
    node_string_decoder_set_method(node_string_decoder_namespace_cache, "end",
                                   node_string_decoder_end, 1);
    node_string_decoder_set_item_property(node_string_decoder_namespace_cache, "default",
                                          node_string_decoder_namespace_cache);
    return node_string_decoder_namespace_cache;
}

static void node_string_decoder_cache_reset(void) {
    node_string_decoder_namespace_cache = (Item){0};
}

int node_string_decoder_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots ||
            !host->node->binary || !host->value || !host->script ||
            !host->value->kind || !host->value->new_object || !host->value->property_set ||
            !host->value->string_from_utf8_n || !host->script->new_function) return -1;
    node_string_decoder_host = host;
    return 0;
}

void node_string_decoder_shutdown(void) {
    node_string_decoder_host = NULL;
}

void node_string_decoder_runtime_attach(void* session) {
    if (!node_string_decoder_host || !node_string_decoder_host->node ||
            !node_string_decoder_host->node->runtime || !node_string_decoder_host->node->roots ||
            !node_string_decoder_host->node->runtime->session_is_live ||
            !node_string_decoder_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_STRING_DECODER,
            sizeof(NodeStringDecoderSessionState))) return;
    node_string_decoder_session = session;
    if (node_string_decoder_host->node->roots->persistent_root_register(session,
            &node_string_decoder_namespace_cache.item) == 0) {
        node_string_decoder_rooted = true;
    }
}

void node_string_decoder_runtime_reset(void* session) {
    if (session != node_string_decoder_session) return;
    // Cached namespace Items belong to the retiring JS heap.
    node_string_decoder_cache_reset();
}

void node_string_decoder_runtime_detach(void* session) {
    if (session != node_string_decoder_session || !node_string_decoder_host) return;
    node_string_decoder_cache_reset();
    if (node_string_decoder_rooted) {
        node_string_decoder_host->node->roots->persistent_root_unregister(session,
            &node_string_decoder_namespace_cache.item);
        node_string_decoder_rooted = false;
    }
    node_string_decoder_session = NULL;
}
