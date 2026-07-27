/**
 * node_zlib_module.cpp — Jube delivery descriptor for Node's zlib surface.
 *
 * Stream constructors remain host-owned while their shared stream state is
 * converted. Stateless codec operations migrate independently through the
 * Jube value/binary boundary.
 */
#include "../../jube/jube.h"

#include <cstring>
#include <math.h>

static const JubeHostAPI* node_zlib_host = NULL;
static void* node_zlib_session = NULL;
static uint64_t node_zlib_namespace_cache = 0;

static Item node_zlib_throw_type_error(const char* code, const char* message) {
    if (!node_zlib_host || !node_zlib_host->node || !node_zlib_host->node->error ||
            !node_zlib_host->node->error->throw_type_error_code) return ItemNull;
    return node_zlib_host->node->error->throw_type_error_code(node_zlib_session, code, message);
}

static Item node_zlib_throw_range_error(const char* code, const char* message) {
    if (!node_zlib_host || !node_zlib_host->node || !node_zlib_host->node->error ||
            !node_zlib_host->node->error->throw_range_error_code) return ItemNull;
    return node_zlib_host->node->error->throw_range_error_code(node_zlib_session, code, message);
}

static bool node_zlib_root_frame(JubeRootFrame* frame, size_t count) {
    return node_zlib_host && node_zlib_host->node && node_zlib_host->node->roots &&
        node_zlib_host->node->roots->root_frame_begin &&
        node_zlib_host->node->roots->root_frame_take_slot &&
        node_zlib_host->node->roots->root_frame_end &&
        node_zlib_host->node->roots->root_frame_begin(frame, count);
}

enum NodeZlibInputStatus {
    NODE_ZLIB_INPUT_OK,
    NODE_ZLIB_INPUT_INVALID,
    NODE_ZLIB_INPUT_DETACHED,
    NODE_ZLIB_INPUT_OUT_OF_BOUNDS,
};

static Item node_zlib_throw_input_error(int status) {
    if (status == NODE_ZLIB_INPUT_DETACHED) {
        return node_zlib_throw_type_error("ERR_INVALID_ARG_TYPE",
                                          "data must reference an attached binary view");
    }
    if (status == NODE_ZLIB_INPUT_OUT_OF_BOUNDS) {
        return node_zlib_throw_range_error("ERR_OUT_OF_RANGE",
                                           "data binary view is outside its backing buffer");
    }
    return node_zlib_throw_type_error("ERR_INVALID_ARG_TYPE",
                                      "data must be a string, Buffer, TypedArray, or DataView");
}

static int node_zlib_read_input(Item data_item, JubeRootFrame* frame,
                                const uint8_t** out_bytes, int* out_length) {
    if (!node_zlib_host || !node_zlib_host->node || !node_zlib_host->node->binary ||
            !node_zlib_host->value || !frame || !out_bytes || !out_length) {
        return NODE_ZLIB_INPUT_INVALID;
    }
    const JubeHostBinaryAPI* binary = node_zlib_host->node->binary;
    const JubeHostValueAPI* value = node_zlib_host->value;
    if (!value->kind || !value->string_length || !value->string_bytes ||
            !binary->is_typed_array || !binary->typed_array_data || !binary->typed_array_length ||
            !binary->describe_view || !binary->buffer_view) return NODE_ZLIB_INPUT_INVALID;

    uint64_t* data_root = node_zlib_host->node->roots->root_frame_take_slot(frame);
    uint64_t* backing_root = node_zlib_host->node->roots->root_frame_take_slot(frame);
    uint64_t* view_root = node_zlib_host->node->roots->root_frame_take_slot(frame);
    if (!data_root || !backing_root || !view_root) {
        return NODE_ZLIB_INPUT_INVALID;
    }
    *data_root = data_item.item;
    *out_bytes = NULL;
    *out_length = 0;
    if (value->kind((Item){.item = *data_root}) == JUBE_VALUE_STRING) {
        Item rooted_data = (Item){.item = *data_root};
        *out_bytes = value->string_bytes(rooted_data);
        size_t string_length = value->string_length(rooted_data);
        if ((*out_bytes == NULL && string_length != 0) || string_length > INT32_MAX) {
            return NODE_ZLIB_INPUT_INVALID;
        }
        *out_length = (int)string_length;
    } else if (binary->is_typed_array((Item){.item = *data_root})) {
        *out_bytes = binary->typed_array_data((Item){.item = *data_root});
        *out_length = binary->typed_array_length((Item){.item = *data_root});
    } else {
        JubeBinaryView view = {};
        int view_status = binary->describe_view((Item){.item = *data_root}, &view);
        if (view_status == JUBE_BINARY_VIEW_NOT_VIEW) {
            return NODE_ZLIB_INPUT_INVALID;
        }
        if (view_status == JUBE_BINARY_VIEW_DETACHED) {
            return NODE_ZLIB_INPUT_DETACHED;
        }
        if (view_status != JUBE_BINARY_VIEW_OK) {
            return NODE_ZLIB_INPUT_OUT_OF_BOUNDS;
        }
        *backing_root = view.array_buffer.item;
        Item byte_view = binary->buffer_view((Item){.item = *backing_root}, view.byte_offset,
                                             view.byte_length);
        *view_root = byte_view.item;
        *out_bytes = binary->typed_array_data((Item){.item = *view_root});
        *out_length = binary->typed_array_length((Item){.item = *view_root});
    }
    if (*out_length < 0 || (*out_length > 0 && !*out_bytes)) {
        return NODE_ZLIB_INPUT_INVALID;
    }
    return NODE_ZLIB_INPUT_OK;
}

static Item node_zlib_crc32(Item data_item, Item seed_item) {
    if (!node_zlib_host || !node_zlib_host->value || !node_zlib_host->script || !node_zlib_session) return ItemNull;
    uint32_t seed = 0;
    int seed_kind = node_zlib_host->value->kind(seed_item);
    if (seed_kind != JUBE_VALUE_UNDEFINED) {
        if (seed_kind != JUBE_VALUE_NUMBER || !node_zlib_host->script->get_number) {
            return node_zlib_throw_type_error("ERR_INVALID_ARG_TYPE", "value must be a number");
        }
        double number = node_zlib_host->script->get_number(seed_item);
        if (!isfinite(number) || number < 0.0 || number > 4294967295.0 ||
                number != (double)(uint32_t)number) {
            return node_zlib_throw_range_error("ERR_OUT_OF_RANGE",
                                               "value must be an unsigned 32-bit integer");
        }
        seed = (uint32_t)number;
    }
    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 3)) return ItemNull;
    const uint8_t* bytes = NULL;
    int length = 0;
    int input_status = node_zlib_read_input(data_item, &frame, &bytes, &length);
    if (input_status != NODE_ZLIB_INPUT_OK) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return node_zlib_throw_input_error(input_status);
    }
    const JubeHostNodeZlibAPI* zlib = node_zlib_host->node->zlib;
    if (!zlib || !zlib->crc32) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    uint32_t result = zlib->crc32(bytes, length, seed);
    node_zlib_host->node->roots->root_frame_end(&frame);
    return (Item){.item = i2it((int64_t)result)};
}

static Item node_zlib_sync_transform(Item data_item, enum JubeNodeZlibCodecMode mode,
                                     const char* method, const char* failure_message) {
    if (!node_zlib_host || !node_zlib_host->node || !node_zlib_host->node->binary ||
            !node_zlib_host->node->zlib || !node_zlib_session) return ItemNull;
    const JubeHostBinaryAPI* binary = node_zlib_host->node->binary;
    const JubeHostNodeZlibAPI* zlib = node_zlib_host->node->zlib;
    if (!binary->buffer_alloc || !binary->buffer_prepare_write || !zlib->codec ||
            !zlib->result_release) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 4)) return ItemNull;
    const uint8_t* bytes = NULL;
    int length = 0;
    int input_status = node_zlib_read_input(data_item, &frame, &bytes, &length);
    if (input_status != NODE_ZLIB_INPUT_OK) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return node_zlib_throw_input_error(input_status);
    }
    JubeNodeZlibResult transformed = {};
    if (!zlib->codec(mode, bytes, length, &transformed)) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        if (node_zlib_host->node->error && node_zlib_host->node->error->throw_zlib_error) {
            return node_zlib_host->node->error->throw_zlib_error(node_zlib_session, method,
                                                                  transformed.status);
        }
        return node_zlib_throw_type_error("ERR_ZLIB", failure_message);
    }
    uint64_t* output_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    if (!output_root) {
        zlib->result_release(&transformed);
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item output = binary->buffer_alloc(transformed.length);
    *output_root = output.item;
    uint8_t* output_bytes = binary->buffer_prepare_write((Item){.item = *output_root});
    if (transformed.length > 0 && !output_bytes) {
        zlib->result_release(&transformed);
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    if (transformed.length > 0) memcpy(output_bytes, transformed.data, (size_t)transformed.length);
    zlib->result_release(&transformed);
    output = (Item){.item = *output_root};
    node_zlib_host->node->roots->root_frame_end(&frame);
    return output;
}

static Item node_zlib_gzip_sync(Item data_item) {
    return node_zlib_sync_transform(data_item, JUBE_NODE_ZLIB_GZIP, "gzip",
                                    "gzip compression failed");
}

static Item node_zlib_gunzip_sync(Item data_item) {
    return node_zlib_sync_transform(data_item, JUBE_NODE_ZLIB_GUNZIP, "gunzip",
                                    "gzip decompression failed");
}

static Item node_zlib_deflate_sync(Item data_item) {
    return node_zlib_sync_transform(data_item, JUBE_NODE_ZLIB_DEFLATE, "deflate",
                                    "deflate compression failed");
}

static Item node_zlib_inflate_sync(Item data_item) {
    return node_zlib_sync_transform(data_item, JUBE_NODE_ZLIB_INFLATE, "inflate",
                                    "deflate decompression failed");
}

static Item node_zlib_deflate_raw_sync(Item data_item) {
    return node_zlib_sync_transform(data_item, JUBE_NODE_ZLIB_DEFLATE_RAW, "deflateRaw",
                                    "raw deflate compression failed");
}

static Item node_zlib_inflate_raw_sync(Item data_item) {
    return node_zlib_sync_transform(data_item, JUBE_NODE_ZLIB_INFLATE_RAW, "inflateRaw",
                                    "raw deflate decompression failed");
}

static Item node_zlib_unzip_sync(Item data_item) {
    return node_zlib_sync_transform(data_item, JUBE_NODE_ZLIB_UNZIP, "unzip",
                                    "compressed data decompression failed");
}

static Item node_zlib_callback_transform(Item data_item, Item options_item, Item callback_item,
                                         enum JubeNodeZlibCodecMode mode, const char* method,
                                         const char* failure_message) {
    if (!node_zlib_host || !node_zlib_host->node || !node_zlib_host->node->async_ops ||
            !node_zlib_host->value || !node_zlib_host->value->kind ||
            !node_zlib_host->node->async_ops->next_tick_callback) return ItemNull;
    if (node_zlib_host->value->kind(options_item) == JUBE_VALUE_FUNCTION &&
            node_zlib_host->value->kind(callback_item) != JUBE_VALUE_FUNCTION) {
        callback_item = options_item;
    }
    if (node_zlib_host->value->kind(callback_item) != JUBE_VALUE_FUNCTION) {
        return node_zlib_throw_type_error("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 3)) return ItemNull;
    uint64_t* callback_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* error_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* result_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    if (!callback_root || !error_root || !result_root) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *callback_root = callback_item.item;
    *error_root = ItemNull.item;
    *result_root = ITEM_JS_UNDEFINED;
    Item result = node_zlib_sync_transform(data_item, mode, method, failure_message);
    if (result.item == ItemNull.item) {
        if (node_zlib_host->script && node_zlib_host->script->check_exception &&
                node_zlib_host->script->clear_exception && node_zlib_host->script->check_exception()) {
            *error_root = node_zlib_host->script->clear_exception().item;
        } else {
            node_zlib_host->node->roots->root_frame_end(&frame);
            return ItemNull;
        }
    } else {
        *result_root = result.item;
    }
    node_zlib_host->node->async_ops->next_tick_callback(
        node_zlib_session, (Item){.item = *callback_root}, (Item){.item = *error_root},
        (Item){.item = *result_root});
    node_zlib_host->node->roots->root_frame_end(&frame);
    return (Item){.item = ITEM_JS_UNDEFINED};
}

#define NODE_ZLIB_CALLBACK_WRAPPER(name, mode, method, message) \
    static Item name(Item data_item, Item options_item, Item callback_item) { \
        return node_zlib_callback_transform(data_item, options_item, callback_item, mode, method, message); \
    }

NODE_ZLIB_CALLBACK_WRAPPER(node_zlib_gzip, JUBE_NODE_ZLIB_GZIP, "gzip", "gzip compression failed")
NODE_ZLIB_CALLBACK_WRAPPER(node_zlib_gunzip, JUBE_NODE_ZLIB_GUNZIP, "gunzip", "gzip decompression failed")
NODE_ZLIB_CALLBACK_WRAPPER(node_zlib_deflate, JUBE_NODE_ZLIB_DEFLATE, "deflate", "deflate compression failed")
NODE_ZLIB_CALLBACK_WRAPPER(node_zlib_inflate, JUBE_NODE_ZLIB_INFLATE, "inflate", "deflate decompression failed")
NODE_ZLIB_CALLBACK_WRAPPER(node_zlib_deflate_raw, JUBE_NODE_ZLIB_DEFLATE_RAW, "deflateRaw", "raw deflate compression failed")
NODE_ZLIB_CALLBACK_WRAPPER(node_zlib_inflate_raw, JUBE_NODE_ZLIB_INFLATE_RAW, "inflateRaw", "raw deflate decompression failed")
NODE_ZLIB_CALLBACK_WRAPPER(node_zlib_unzip, JUBE_NODE_ZLIB_UNZIP, "unzip", "compressed data decompression failed")

#undef NODE_ZLIB_CALLBACK_WRAPPER

static void node_zlib_install_method(uint64_t* namespace_root, uint64_t* key_root,
                                     uint64_t* function_root, const char* name,
                                     int name_length, void* implementation, int parameter_count) {
    Item key = node_zlib_host->value->string_from_utf8_n(name, name_length);
    *key_root = key.item;
    Item function = node_zlib_host->script->new_function(implementation, parameter_count);
    *function_root = function.item;
    node_zlib_host->value->property_set((Item){.item = *namespace_root},
                                        (Item){.item = *key_root},
                                        (Item){.item = *function_root});
}

static Item node_zlib_namespace(void) {
    if (!node_zlib_host || !node_zlib_session || !node_zlib_host->node ||
            !node_zlib_host->node->runtime ||
            !node_zlib_host->node->runtime->resolve_host_namespace) {
        return ItemNull;
    }
    if (node_zlib_namespace_cache != 0) return (Item){.item = node_zlib_namespace_cache};
    Item result = ItemNull;
    if (node_zlib_host->node->runtime->resolve_host_namespace(node_zlib_session, "zlib", &result) != 0) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 3)) return ItemNull;
    uint64_t* namespace_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root || !function_root || !node_zlib_host->value ||
            !node_zlib_host->value->string_from_utf8_n || !node_zlib_host->value->property_set ||
            !node_zlib_host->script || !node_zlib_host->script->new_function) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *namespace_root = result.item;
    Item key = node_zlib_host->value->string_from_utf8_n("crc32", 5);
    *key_root = key.item;
    Item function = node_zlib_host->script->new_function((void*)node_zlib_crc32, 2);
    *function_root = function.item;
    node_zlib_host->value->property_set((Item){.item = *namespace_root},
                                        (Item){.item = *key_root},
                                        (Item){.item = *function_root});
    node_zlib_install_method(namespace_root, key_root, function_root, "gzipSync", 8, (void*)node_zlib_gzip_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "gunzipSync", 10, (void*)node_zlib_gunzip_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "deflateSync", 11, (void*)node_zlib_deflate_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "inflateSync", 11, (void*)node_zlib_inflate_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "deflateRawSync", 14, (void*)node_zlib_deflate_raw_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "inflateRawSync", 14, (void*)node_zlib_inflate_raw_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "unzipSync", 9, (void*)node_zlib_unzip_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "gzip", 4, (void*)node_zlib_gzip, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "gunzip", 6, (void*)node_zlib_gunzip, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "deflate", 7, (void*)node_zlib_deflate, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "inflate", 7, (void*)node_zlib_inflate, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "deflateRaw", 10, (void*)node_zlib_deflate_raw, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "inflateRaw", 10, (void*)node_zlib_inflate_raw, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "unzip", 5, (void*)node_zlib_unzip, 3);
    node_zlib_namespace_cache = *namespace_root;
    node_zlib_host->node->roots->root_frame_end(&frame);
    return result;
}

static int node_zlib_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots || !host->value ||
            !host->script || !host->node->binary || !host->node->error || !host->node->zlib ||
            !host->node->async_ops ||
            host->node->binary->struct_size < sizeof(JubeHostBinaryAPI) ||
            host->node->error->struct_size < sizeof(JubeHostNodeErrorAPI) ||
            host->node->zlib->struct_size < sizeof(JubeHostNodeZlibAPI) ||
            host->node->async_ops->struct_size < sizeof(JubeHostAsyncAPI) ||
            !host->node->binary->buffer_alloc || !host->node->binary->buffer_prepare_write ||
            !host->node->error->throw_zlib_error ||
            !host->node->zlib->crc32 || !host->node->zlib->codec || !host->node->zlib->result_release ||
            !host->node->async_ops->next_tick_callback ||
            !host->script->check_exception || !host->script->clear_exception) return -1;
    node_zlib_host = host;
    return 0;
}

static void node_zlib_shutdown(void) {
    node_zlib_namespace_cache = 0;
    node_zlib_session = NULL;
    node_zlib_host = NULL;
}

static void node_zlib_runtime_attach(void* session) {
    node_zlib_session = session;
    if (node_zlib_host && node_zlib_host->node && node_zlib_host->node->roots &&
            node_zlib_host->node->roots->persistent_root_register) {
        (void)node_zlib_host->node->roots->persistent_root_register(session, &node_zlib_namespace_cache);
    }
}
static void node_zlib_runtime_reset(void* session) {
    if (session == node_zlib_session) node_zlib_namespace_cache = 0;
}
static void node_zlib_runtime_detach(void* session) {
    if (node_zlib_host && node_zlib_host->node && node_zlib_host->node->roots &&
            node_zlib_host->node->roots->persistent_root_unregister) {
        (void)node_zlib_host->node->roots->persistent_root_unregister(session, &node_zlib_namespace_cache);
    }
    if (session == node_zlib_session) node_zlib_namespace_cache = 0;
    if (session == node_zlib_session) node_zlib_session = NULL;
}

static const char* const node_zlib_specifiers[] = { "zlib" };

static const JubeNamespaceDef node_zlib_namespaces[] = {
    {node_zlib_specifiers, 1, node_zlib_namespace, NULL, 0},
};

static const JubeModuleRequirements node_zlib_requirements = {
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

static const char* const node_zlib_dependencies[] = { "node-core" };

static const JubeModuleDef node_zlib_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "node-zlib",
    "0.1.0",
    "Node zlib dynamic delivery bridge",
    NULL,
    0,
    NULL,
    0,
    node_zlib_namespaces,
    1,
    node_zlib_init,
    node_zlib_shutdown,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    &node_zlib_requirements,
    NULL,
    0,
    node_zlib_runtime_attach,
    node_zlib_runtime_reset,
    node_zlib_runtime_detach,
    node_zlib_dependencies,
    1,
};

extern "C" const JubeModuleDef* jube_module(void) { return &node_zlib_module; }
