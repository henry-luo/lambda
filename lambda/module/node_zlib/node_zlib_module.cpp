/**
 * node_zlib_module.cpp — Jube delivery descriptor for Node's zlib surface.
 *
 * The namespace and Node-facing stream lifecycle live in this Jube image.
 * Compression state and raw zlib calls remain behind the host provider so the
 * dynamic image has no dependency-library imports.
 */
#include "../../jube/jube.h"

#include <climits>
#include <cstring>
#include <math.h>
#include <stdlib.h>

static const JubeHostAPI* node_zlib_host = NULL;
static void* node_zlib_session = NULL;
static uint64_t node_zlib_namespace_cache = 0;
static uint64_t node_zlib_state_key_cache = 0;
static uint64_t node_zlib_mode_key_cache = 0;
static uint64_t node_zlib_transform_prototype_cache = 0;
static uint64_t node_zlib_constructor_prototypes[8] = {};

static bool node_zlib_root_frame(JubeRootFrame* frame, size_t count);

enum NodeZlibTransformMode {
    NODE_ZLIB_TRANSFORM_GZIP = 1,
    NODE_ZLIB_TRANSFORM_GUNZIP,
    NODE_ZLIB_TRANSFORM_DEFLATE,
    NODE_ZLIB_TRANSFORM_INFLATE,
    NODE_ZLIB_TRANSFORM_DEFLATE_RAW,
    NODE_ZLIB_TRANSFORM_INFLATE_RAW,
    NODE_ZLIB_TRANSFORM_UNZIP,
};

enum NodeZlibFlushMode {
    NODE_ZLIB_NO_FLUSH = 0,
    NODE_ZLIB_PARTIAL_FLUSH = 1,
    NODE_ZLIB_SYNC_FLUSH = 2,
    NODE_ZLIB_FULL_FLUSH = 3,
    NODE_ZLIB_FINISH = 4,
};

enum NodeZlibStatus {
    NODE_ZLIB_STREAM_END = 1,
    NODE_ZLIB_ERRNO = -1,
    NODE_ZLIB_STREAM_ERROR = -2,
    NODE_ZLIB_DATA_ERROR = -3,
    NODE_ZLIB_MEM_ERROR = -4,
    NODE_ZLIB_BUF_ERROR = -5,
    NODE_ZLIB_VERSION_ERROR = -6,
};

struct NodeZlibStreamState {
    const JubeHostNodeZlibAPI* zlib;
    void* host_state;
    int mode;
    bool is_deflate;
};

static void node_zlib_stream_state_destroy(void* native) {
    NodeZlibStreamState* state = (NodeZlibStreamState*)native;
    if (!state) return;
    if (state->host_state && state->zlib && state->zlib->stream_free) {
        state->zlib->stream_free(state->host_state);
    }
    free(state);
}

static const JubeTypeDef node_zlib_stream_state_type = {
    "NodeZlibStreamState",
    JUBE_TYPE_OWNING_NATIVE,
    NULL,
    node_zlib_stream_state_destroy,
};

static Item node_zlib_undefined(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

// avoid Clang 14's designated-initializer failure in instantiated templates.
static Item node_zlib_item_from_word(uint64_t word) {
    Item result = {};
    result.item = word;
    return result;
}

static bool node_zlib_is_undefined(Item value) {
    return !node_zlib_host || !node_zlib_host->value ||
        value.item == 0 || node_zlib_host->value->kind(value) == JUBE_VALUE_UNDEFINED;
}

static bool node_zlib_is_callable(Item value) {
    return node_zlib_host && node_zlib_host->value &&
        node_zlib_host->value->kind(value) == JUBE_VALUE_FUNCTION;
}

static Item node_zlib_key(const char* text, size_t length) {
    return node_zlib_host->value->string_from_utf8_n(text, length);
}

static Item node_zlib_state_key(void) {
    if (node_zlib_state_key_cache == 0) {
        node_zlib_state_key_cache = node_zlib_key("__zlib_state__", 14).item;
    }
    return (Item){.item = node_zlib_state_key_cache};
}

static Item node_zlib_mode_key(void) {
    if (node_zlib_mode_key_cache == 0) {
        node_zlib_mode_key_cache = node_zlib_key("__zlib_mode__", 13).item;
    }
    return (Item){.item = node_zlib_mode_key_cache};
}

static Item node_zlib_transform_prototype(void) {
    if (node_zlib_transform_prototype_cache != 0) {
        return (Item){.item = node_zlib_transform_prototype_cache};
    }
    if (!node_zlib_host || !node_zlib_host->node || !node_zlib_host->node->streams ||
            !node_zlib_host->node->streams->transform_prototype) return ItemNull;
    Item prototype = node_zlib_host->node->streams->transform_prototype();
    if (node_zlib_host->value->kind(prototype) != JUBE_VALUE_OBJECT) return ItemNull;
    node_zlib_transform_prototype_cache = prototype.item;
    return prototype;
}

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

static Item node_zlib_error_payload(const char* method, int status) {
    if (!node_zlib_host || !node_zlib_host->node || !node_zlib_host->node->error ||
            !node_zlib_host->node->error->throw_zlib_error ||
            !node_zlib_host->script || !node_zlib_host->script->error_lane_payload) {
        return ItemNull;
    }
    Item lane = node_zlib_host->node->error->throw_zlib_error(
        node_zlib_session, method, status);
    return node_zlib_host->script->error_lane_payload(lane);
}

static bool node_zlib_option_number(Item options, const char* name, double* out_number) {
    if (!node_zlib_host || !node_zlib_host->value || !name || !out_number ||
            node_zlib_host->value->kind(options) != JUBE_VALUE_OBJECT) return false;
    Item key = node_zlib_key(name, strlen(name));
    Item value = node_zlib_host->value->property_get(options, key);
    int kind = node_zlib_host->value->kind(value);
    if (kind != JUBE_VALUE_NUMBER || !node_zlib_host->script ||
            !node_zlib_host->script->get_number) return false;
    *out_number = node_zlib_host->script->get_number(value);
    return *out_number == *out_number && *out_number != 1.0 / 0.0 &&
        *out_number != -1.0 / 0.0;
}

static int node_zlib_option_int(Item options, const char* name, int fallback) {
    double number = 0.0;
    if (!node_zlib_option_number(options, name, &number) ||
            number != (double)(int)number) return fallback;
    return (int)number;
}

static bool node_zlib_is_deflate(int mode) {
    return mode == NODE_ZLIB_TRANSFORM_GZIP || mode == NODE_ZLIB_TRANSFORM_DEFLATE ||
        mode == NODE_ZLIB_TRANSFORM_DEFLATE_RAW;
}

static const char* node_zlib_mode_name(int mode) {
    switch (mode) {
    case NODE_ZLIB_TRANSFORM_GZIP: return "Gzip";
    case NODE_ZLIB_TRANSFORM_GUNZIP: return "Gunzip";
    case NODE_ZLIB_TRANSFORM_DEFLATE: return "Deflate";
    case NODE_ZLIB_TRANSFORM_INFLATE: return "Inflate";
    case NODE_ZLIB_TRANSFORM_DEFLATE_RAW: return "DeflateRaw";
    case NODE_ZLIB_TRANSFORM_INFLATE_RAW: return "InflateRaw";
    case NODE_ZLIB_TRANSFORM_UNZIP: return "Unzip";
    default: return "Zlib";
    }
}

static const char* node_zlib_constructor_name(int mode, int* out_length) {
    const char* name = node_zlib_mode_name(mode);
    if (out_length) *out_length = (int)strlen(name);
    return name;
}

static bool node_zlib_validate_int_option(Item options, const char* key_name,
                                          const char* prop_name, int min_value,
                                          int max_value, bool allow_zero) {
    if (node_zlib_host->value->kind(options) != JUBE_VALUE_OBJECT) return true;
    Item key = node_zlib_key(key_name, strlen(key_name));
    Item value = node_zlib_host->value->property_get(options, key);
    if (node_zlib_is_undefined(value)) return true;
    int kind = node_zlib_host->value->kind(value);
    if (kind != JUBE_VALUE_NUMBER || !node_zlib_host->script->get_number) {
        node_zlib_throw_type_error("ERR_INVALID_ARG_TYPE", prop_name);
        return false;
    }
    double number = node_zlib_host->script->get_number(value);
    bool integral = number == (double)(int)number;
    bool zero_is_allowed = allow_zero && number == 0.0;
    if (!integral || number != number || number == 1.0 / 0.0 || number == -1.0 / 0.0 ||
            (!zero_is_allowed && (number < min_value || number > max_value))) {
        node_zlib_throw_range_error("ERR_OUT_OF_RANGE", prop_name);
        return false;
    }
    return true;
}

static bool node_zlib_validate_stream_options(int mode, Item options) {
    if (node_zlib_host->value->kind(options) != JUBE_VALUE_OBJECT) return true;
    bool is_deflate = node_zlib_is_deflate(mode);
    int min_window_bits = mode == NODE_ZLIB_TRANSFORM_GZIP ? 9 : 8;
    if (!node_zlib_validate_int_option(options, "windowBits", "options.windowBits",
            min_window_bits, 15, !is_deflate)) return false;
    if (is_deflate) {
        if (!node_zlib_validate_int_option(options, "level", "options.level", -1, 9, false)) return false;
        if (!node_zlib_validate_int_option(options, "memLevel", "options.memLevel", 1, 9, false)) return false;
        if (!node_zlib_validate_int_option(options, "strategy", "options.strategy", 0, 4, false)) return false;
    }
    return true;
}

static NodeZlibStreamState* node_zlib_stream_state_from_stream(Item stream) {
    if (!node_zlib_host || !node_zlib_host->value ||
            !node_zlib_host->value->native_object_data) return NULL;
    Item state_item = node_zlib_host->value->property_get(stream, node_zlib_state_key());
    if (node_zlib_host->value->kind(state_item) != JUBE_VALUE_OBJECT) return NULL;
    return (NodeZlibStreamState*)node_zlib_host->value->native_object_data(
        state_item, &node_zlib_stream_state_type);
}

static void node_zlib_stream_state_close(NodeZlibStreamState* state) {
    if (!state || !state->host_state) return;
    if (state->zlib && state->zlib->stream_free) state->zlib->stream_free(state->host_state);
    state->host_state = NULL;
}

static Item node_zlib_stream_result(JubeNodeZlibResult* result) {
    if (!result || !node_zlib_host || !node_zlib_host->node ||
            !node_zlib_host->node->binary || !node_zlib_host->node->binary->buffer_from_bytes) {
        return ItemNull;
    }
    Item output = node_zlib_host->node->binary->buffer_from_bytes(result->data, result->length);
    if (node_zlib_host->node->zlib && node_zlib_host->node->zlib->result_release) {
        node_zlib_host->node->zlib->result_release(result);
    }
    return output;
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

// brotli remains unavailable until the host exposes a versioned provider.
static Item node_zlib_brotli_compress_sync(Item data_item) {
    (void)data_item;
    return ItemNull;
}

static Item node_zlib_brotli_decompress_sync(Item data_item) {
    (void)data_item;
    return ItemNull;
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
    if (item_is_error(result)) {
        *error_root = node_zlib_host->script->error_lane_payload(result).item;
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

static bool node_zlib_run_stream(NodeZlibStreamState* state, const uint8_t* data, int length,
                                  int flush, JubeNodeZlibResult* result) {
    if (!state || !state->host_state || !state->zlib || !state->zlib->stream_run) return false;
    return state->zlib->stream_run(state->host_state, data, length, flush, result);
}

static void node_zlib_push_result(Item stream, JubeNodeZlibResult* result) {
    if (!result || result->length <= 0) {
        if (result && node_zlib_host->node->zlib->result_release) {
            node_zlib_host->node->zlib->result_release(result);
        }
        return;
    }
    Item output = node_zlib_stream_result(result);
    if (node_zlib_host->node->streams && node_zlib_host->node->streams->readable_push) {
        node_zlib_host->node->streams->readable_push(stream, output);
        if (node_zlib_host->node->streams->flush_data_if_flowing) {
            node_zlib_host->node->streams->flush_data_if_flowing(stream);
        }
    }
}

static Item node_zlib_transform_chunk(Item chunk, Item encoding, Item callback) {
    (void)encoding;
    if (!node_zlib_host || !node_zlib_host->script || !node_zlib_host->node ||
            !node_zlib_host->node->streams) return node_zlib_undefined();
    Item stream = node_zlib_host->script->current_this();
    NodeZlibStreamState* state = node_zlib_stream_state_from_stream(stream);
    if (!state) {
        Item error = node_zlib_error_payload("zlib", NODE_ZLIB_STREAM_ERROR);
        if (node_zlib_is_callable(callback)) node_zlib_host->script->call_function(
            callback, node_zlib_undefined(), &error, 1);
        return node_zlib_undefined();
    }

    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 8)) return node_zlib_undefined();
    uint64_t* stream_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* callback_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    if (!stream_root || !callback_root) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return node_zlib_undefined();
    }
    *stream_root = stream.item;
    *callback_root = callback.item;
    const uint8_t* input = NULL;
    int input_length = 0;
    int input_status = node_zlib_read_input(chunk, &frame, &input, &input_length);
    if (input_status != NODE_ZLIB_INPUT_OK) {
        Item rooted_callback = (Item){.item = *callback_root};
        Item error = node_zlib_throw_input_error(input_status);
        if (node_zlib_is_callable(rooted_callback)) node_zlib_host->script->call_function(
            rooted_callback, node_zlib_undefined(), &error, 1);
        node_zlib_host->node->roots->root_frame_end(&frame);
        return node_zlib_undefined();
    }

    JubeNodeZlibResult result = {};
    int status = NODE_ZLIB_STREAM_ERROR;
    bool ok = node_zlib_run_stream(state, input, input_length, NODE_ZLIB_NO_FLUSH, &result);
    status = result.status;
    if (!ok) {
        Item rooted_callback = (Item){.item = *callback_root};
        Item error = node_zlib_error_payload(node_zlib_mode_name(state->mode), status);
        if (node_zlib_is_callable(rooted_callback)) node_zlib_host->script->call_function(
            rooted_callback, node_zlib_undefined(), &error, 1);
        node_zlib_host->node->roots->root_frame_end(&frame);
        return node_zlib_undefined();
    }
    if (result.length > 0) {
        uint64_t* output_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
        if (!output_root) {
            node_zlib_host->node->zlib->result_release(&result);
            node_zlib_host->node->roots->root_frame_end(&frame);
            return node_zlib_undefined();
        }
        Item output = node_zlib_stream_result(&result);
        *output_root = output.item;
        node_zlib_host->node->streams->readable_push(
            (Item){.item = *stream_root}, (Item){.item = *output_root});
        if (node_zlib_host->node->streams->flush_data_if_flowing) {
            node_zlib_host->node->streams->flush_data_if_flowing(
                (Item){.item = *stream_root});
        }
    }
    if (node_zlib_is_callable((Item){.item = *callback_root})) {
        Item args[1] = { ItemNull };
        node_zlib_host->script->call_function((Item){.item = *callback_root},
            node_zlib_undefined(), args, 1);
    }
    node_zlib_host->node->roots->root_frame_end(&frame);
    return node_zlib_undefined();
}

static Item node_zlib_transform_flush(Item callback) {
    if (!node_zlib_host || !node_zlib_host->script || !node_zlib_host->node ||
            !node_zlib_host->node->streams) return node_zlib_undefined();
    Item stream = node_zlib_host->script->current_this();
    NodeZlibStreamState* state = node_zlib_stream_state_from_stream(stream);
    if (!state) {
        Item error = node_zlib_error_payload("zlib", NODE_ZLIB_STREAM_ERROR);
        if (node_zlib_is_callable(callback)) node_zlib_host->script->call_function(
            callback, node_zlib_undefined(), &error, 1);
        return node_zlib_undefined();
    }
    JubeNodeZlibResult result = {};
    bool ok = node_zlib_run_stream(state, NULL, 0, NODE_ZLIB_FINISH, &result);
    int status = result.status;
    if (ok) node_zlib_push_result(stream, &result);
    node_zlib_stream_state_close(state);
    if (!ok) {
        Item error = node_zlib_error_payload(node_zlib_mode_name(state->mode), status);
        if (node_zlib_is_callable(callback)) node_zlib_host->script->call_function(
            callback, node_zlib_undefined(), &error, 1);
        return node_zlib_undefined();
    }
    if (node_zlib_host->node->streams->transform_flush_drained) {
        node_zlib_host->node->streams->transform_flush_drained(stream);
    }
    if (node_zlib_is_callable(callback)) node_zlib_host->script->call_function(
        callback, node_zlib_undefined(), NULL, 0);
    return node_zlib_undefined();
}

static Item node_zlib_transform_destroy(Item error, Item callback) {
    if (!node_zlib_host || !node_zlib_host->script) return node_zlib_undefined();
    Item stream = node_zlib_host->script->current_this();
    node_zlib_stream_state_close(node_zlib_stream_state_from_stream(stream));
    if (node_zlib_is_callable(callback)) {
        if (node_zlib_host->value->kind(error) != JUBE_VALUE_NULL &&
                !node_zlib_is_undefined(error)) {
            node_zlib_host->script->call_function(callback, node_zlib_undefined(), &error, 1);
        } else {
            Item args[1] = { ItemNull };
            node_zlib_host->script->call_function(callback, node_zlib_undefined(), args, 1);
        }
    }
    return node_zlib_undefined();
}

static Item node_zlib_stream_flush_method(Item kind, Item callback) {
    if (!node_zlib_host || !node_zlib_host->script || !node_zlib_host->node ||
            !node_zlib_host->node->streams) return node_zlib_undefined();
    if (node_zlib_is_callable(kind) && !node_zlib_is_callable(callback)) {
        callback = kind;
        kind = node_zlib_undefined();
    }
    int flush = NODE_ZLIB_FULL_FLUSH;
    if (!node_zlib_is_undefined(kind) && node_zlib_host->value->kind(kind) == JUBE_VALUE_NUMBER) {
        double number = node_zlib_host->script->get_number(kind);
        if (number == (double)(int)number) flush = (int)number;
    }
    Item stream = node_zlib_host->script->current_this();
    NodeZlibStreamState* state = node_zlib_stream_state_from_stream(stream);
    if (!state) {
        Item error = node_zlib_error_payload("zlib", NODE_ZLIB_STREAM_ERROR);
        if (node_zlib_is_callable(callback)) node_zlib_host->script->call_function(
            callback, node_zlib_undefined(), &error, 1);
        return node_zlib_undefined();
    }
    JubeNodeZlibResult result = {};
    bool ok = node_zlib_run_stream(state, NULL, 0, flush, &result);
    int status = result.status;
    if (ok) node_zlib_push_result(stream, &result);
    if (!ok) {
        Item error = node_zlib_error_payload(node_zlib_mode_name(state->mode), status);
        if (node_zlib_is_callable(callback)) node_zlib_host->script->call_function(
            callback, node_zlib_undefined(), &error, 1);
        return node_zlib_undefined();
    }
    if (node_zlib_host->node->streams->transform_flush_drained) {
        node_zlib_host->node->streams->transform_flush_drained(stream);
    }
    if (node_zlib_is_callable(callback)) node_zlib_host->script->call_function(
        callback, node_zlib_undefined(), NULL, 0);
    return node_zlib_undefined();
}

template <typename Target>
static void node_zlib_set_instance_method(Item stream, const char* name, Target target,
                                           int adapter_arity) {
    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 3)) return;
    uint64_t* stream_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    if (!stream_root || !key_root || !function_root) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return;
    }
    *stream_root = stream.item;
    *key_root = node_zlib_key(name, strlen(name)).item;
    *function_root = jube_new_function(node_zlib_host->script, target, adapter_arity).item;
    node_zlib_host->value->property_set(node_zlib_item_from_word(*stream_root),
        node_zlib_item_from_word(*key_root), node_zlib_item_from_word(*function_root));
    node_zlib_host->script->mark_non_enumerable(node_zlib_item_from_word(*stream_root),
        node_zlib_item_from_word(*key_root));
    node_zlib_host->node->roots->root_frame_end(&frame);
}

static Item node_zlib_create_transform(int mode, Item options) {
    if (!node_zlib_host || !node_zlib_host->node || !node_zlib_host->node->streams ||
            !node_zlib_host->node->streams->transform_new || !node_zlib_host->node->zlib ||
            !node_zlib_host->node->zlib->stream_init) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 9)) return ItemNull;
    uint64_t* options_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* stream_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* state_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* mode_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* constructor_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* constructor_prototype_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    if (!options_root || !stream_root || !state_root || !mode_root ||
            !constructor_root || !constructor_prototype_root) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *options_root = options.item;
    options = (Item){.item = *options_root};
    if (!node_zlib_validate_stream_options(mode, options)) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    int window_bits = node_zlib_option_int(options, "windowBits", 15);
    if (window_bits <= 0) window_bits = 15;
    int host_mode = mode - 1;
    switch (mode) {
    case NODE_ZLIB_TRANSFORM_GZIP:
    case NODE_ZLIB_TRANSFORM_GUNZIP: window_bits += 16; break;
    case NODE_ZLIB_TRANSFORM_DEFLATE_RAW:
    case NODE_ZLIB_TRANSFORM_INFLATE_RAW: window_bits = -window_bits; break;
    case NODE_ZLIB_TRANSFORM_UNZIP: window_bits += 32; break;
    default: break;
    }
    int level = node_zlib_option_int(options, "level", -1);
    int mem_level = node_zlib_option_int(options, "memLevel", 8);
    int strategy = node_zlib_option_int(options, "strategy", 0);
    void* host_state = NULL;
    int status = NODE_ZLIB_STREAM_ERROR;
    if (!node_zlib_host->node->zlib->stream_init((enum JubeNodeZlibCodecMode)host_mode,
            window_bits, level, mem_level, strategy, &host_state, &status)) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return node_zlib_error_payload(node_zlib_mode_name(mode), status);
    }
    NodeZlibStreamState* state = (NodeZlibStreamState*)calloc(1, sizeof(NodeZlibStreamState));
    if (!state) {
        node_zlib_host->node->zlib->stream_free(host_state);
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    state->zlib = node_zlib_host->node->zlib;
    state->host_state = host_state;
    state->mode = mode;
    state->is_deflate = node_zlib_is_deflate(mode);
    Item transform_proto = node_zlib_transform_prototype();
    if (node_zlib_host->value->kind(transform_proto) != JUBE_VALUE_OBJECT) {
        node_zlib_stream_state_destroy(state);
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item stream = node_zlib_host->node->streams->transform_new(options);
    if (node_zlib_host->value->kind(stream) == JUBE_VALUE_NULL) {
        node_zlib_stream_state_destroy(state);
        node_zlib_host->node->roots->root_frame_end(&frame);
        return stream;
    }
    *stream_root = stream.item;
    *state_root = node_zlib_host->value->native_object_new(&node_zlib_stream_state_type, state).item;
    if (*state_root == ItemNull.item) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        node_zlib_stream_state_destroy(state);
        return ItemNull;
    }
    *mode_root = i2it(mode);
    node_zlib_host->value->property_set((Item){.item = *stream_root}, node_zlib_state_key(),
        (Item){.item = *state_root});
    node_zlib_host->value->property_set((Item){.item = *stream_root}, node_zlib_mode_key(),
        (Item){.item = *mode_root});
    node_zlib_host->script->mark_non_enumerable((Item){.item = *stream_root}, node_zlib_state_key());
    node_zlib_host->script->mark_non_enumerable((Item){.item = *stream_root}, node_zlib_mode_key());
    node_zlib_set_instance_method((Item){.item = *stream_root}, "_transform",
        node_zlib_transform_chunk, 3);
    node_zlib_set_instance_method((Item){.item = *stream_root}, "_flush",
        node_zlib_transform_flush, 1);
    node_zlib_set_instance_method((Item){.item = *stream_root}, "_destroy",
        node_zlib_transform_destroy, 2);
    node_zlib_set_instance_method((Item){.item = *stream_root}, "flush",
        node_zlib_stream_flush_method, 2);
    if (mode >= NODE_ZLIB_TRANSFORM_GZIP && mode <= NODE_ZLIB_TRANSFORM_UNZIP &&
            node_zlib_namespace_cache != 0) {
        int constructor_name_length = 0;
        const char* constructor_name = node_zlib_constructor_name(mode,
                                                                    &constructor_name_length);
        *constructor_root = node_zlib_host->value->property_get(
            (Item){.item = node_zlib_namespace_cache},
            node_zlib_key(constructor_name, (size_t)constructor_name_length)).item;
        *constructor_prototype_root = node_zlib_host->value->property_get(
            (Item){.item = *constructor_root}, node_zlib_key("prototype", 9)).item;
        Item constructor_prototype = (Item){.item = *constructor_prototype_root};
        node_zlib_host->script->set_prototype(constructor_prototype, transform_proto);
        node_zlib_host->script->set_prototype((Item){.item = *stream_root},
            constructor_prototype);
    }
    stream = (Item){.item = *stream_root};
    node_zlib_host->node->roots->root_frame_end(&frame);
    return stream;
}

#define NODE_ZLIB_TRANSFORM_WRAPPER(name, mode) \
    static Item name(Item options) { return node_zlib_create_transform(mode, options); }
NODE_ZLIB_TRANSFORM_WRAPPER(node_zlib_create_gzip, NODE_ZLIB_TRANSFORM_GZIP)
NODE_ZLIB_TRANSFORM_WRAPPER(node_zlib_create_gunzip, NODE_ZLIB_TRANSFORM_GUNZIP)
NODE_ZLIB_TRANSFORM_WRAPPER(node_zlib_create_deflate, NODE_ZLIB_TRANSFORM_DEFLATE)
NODE_ZLIB_TRANSFORM_WRAPPER(node_zlib_create_inflate, NODE_ZLIB_TRANSFORM_INFLATE)
NODE_ZLIB_TRANSFORM_WRAPPER(node_zlib_create_deflate_raw, NODE_ZLIB_TRANSFORM_DEFLATE_RAW)
NODE_ZLIB_TRANSFORM_WRAPPER(node_zlib_create_inflate_raw, NODE_ZLIB_TRANSFORM_INFLATE_RAW)
NODE_ZLIB_TRANSFORM_WRAPPER(node_zlib_create_unzip, NODE_ZLIB_TRANSFORM_UNZIP)
#undef NODE_ZLIB_TRANSFORM_WRAPPER

template <typename Target>
static void node_zlib_install_method(uint64_t* namespace_root,
        uint64_t* key_root, uint64_t* function_root, const char* name,
        int name_length, Target target, int adapter_arity) {
    Item key = node_zlib_host->value->string_from_utf8_n(name, name_length);
    *key_root = key.item;
    Item function = jube_new_function(node_zlib_host->script, target,
        adapter_arity);
    *function_root = function.item;
    node_zlib_host->value->property_set(node_zlib_item_from_word(*namespace_root),
                                        node_zlib_item_from_word(*key_root),
                                        node_zlib_item_from_word(*function_root));
}

template <typename Target>
static Item node_zlib_install_constructor(uint64_t* namespace_root, const char* name,
                                          int name_length, Target target, int mode,
                                          Item transform_proto) {
    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 7)) return ItemNull;
    uint64_t* namespace_slot = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_slot = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* constructor_slot = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* prototype_slot = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* transform_proto_slot = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* constructor_key_slot = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* prototype_key_slot = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_slot || !key_slot || !constructor_slot || !prototype_slot ||
            !transform_proto_slot || !constructor_key_slot || !prototype_key_slot) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *namespace_slot = *namespace_root;
    *transform_proto_slot = transform_proto.item;
    *key_slot = node_zlib_key(name, (size_t)name_length).item;
    *constructor_slot = jube_new_constructor(node_zlib_host->script, target, 1).item;
    *prototype_slot = node_zlib_host->value->new_object().item;
    Item prototype = node_zlib_item_from_word(*prototype_slot);
    if (node_zlib_host->value->kind(node_zlib_item_from_word(*transform_proto_slot)) == JUBE_VALUE_OBJECT) {
        node_zlib_host->script->set_prototype(prototype,
            node_zlib_item_from_word(*transform_proto_slot));
    }
    *constructor_key_slot = node_zlib_key("constructor", 11).item;
    node_zlib_host->value->property_set(prototype, node_zlib_item_from_word(*constructor_key_slot),
        node_zlib_item_from_word(*constructor_slot));
    node_zlib_host->script->mark_non_enumerable(prototype,
        node_zlib_item_from_word(*constructor_key_slot));
    *prototype_key_slot = node_zlib_key("prototype", 9).item;
    node_zlib_host->value->property_set(node_zlib_item_from_word(*constructor_slot),
        node_zlib_item_from_word(*prototype_key_slot), prototype);
    node_zlib_host->script->function_set_prototype(node_zlib_item_from_word(*constructor_slot), prototype);
    node_zlib_host->script->set_function_name(node_zlib_item_from_word(*constructor_slot),
        node_zlib_item_from_word(*key_slot));
    node_zlib_constructor_prototypes[mode] = *prototype_slot;
    node_zlib_host->value->property_set(node_zlib_item_from_word(*namespace_slot),
        node_zlib_item_from_word(*key_slot), node_zlib_item_from_word(*constructor_slot));
    node_zlib_host->node->roots->root_frame_end(&frame);
    return node_zlib_item_from_word(*constructor_slot);
}

static Item node_zlib_namespace(void) {
    if (!node_zlib_host || !node_zlib_session || !node_zlib_host->node ||
            !node_zlib_host->node->runtime ||
            !node_zlib_host->value || !node_zlib_host->script) {
        return ItemNull;
    }
    if (node_zlib_namespace_cache != 0) return (Item){.item = node_zlib_namespace_cache};

    JubeRootFrame frame = {};
    if (!node_zlib_root_frame(&frame, 8)) return ItemNull;
    uint64_t* namespace_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* stream_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* transform_ctor_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* transform_proto_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* constants_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* codes_root = node_zlib_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !stream_root || !transform_ctor_root || !transform_proto_root ||
            !key_root || !function_root || !constants_root || !codes_root ||
            !node_zlib_host->value->new_object ||
            !node_zlib_host->value->property_set || !node_zlib_host->script->new_function) {
        node_zlib_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *namespace_root = node_zlib_host->value->new_object().item;
    *stream_root = 0;
    *transform_ctor_root = 0;
    *transform_proto_root = 0;
    node_zlib_namespace_cache = *namespace_root;

    node_zlib_install_constructor(namespace_root, "Gzip", 4,
        node_zlib_create_gzip, NODE_ZLIB_TRANSFORM_GZIP, (Item){.item = *transform_proto_root});
    node_zlib_install_constructor(namespace_root, "Gunzip", 6,
        node_zlib_create_gunzip, NODE_ZLIB_TRANSFORM_GUNZIP, (Item){.item = *transform_proto_root});
    node_zlib_install_constructor(namespace_root, "Deflate", 7,
        node_zlib_create_deflate, NODE_ZLIB_TRANSFORM_DEFLATE, (Item){.item = *transform_proto_root});
    node_zlib_install_constructor(namespace_root, "Inflate", 7,
        node_zlib_create_inflate, NODE_ZLIB_TRANSFORM_INFLATE, (Item){.item = *transform_proto_root});
    node_zlib_install_constructor(namespace_root, "DeflateRaw", 10,
        node_zlib_create_deflate_raw, NODE_ZLIB_TRANSFORM_DEFLATE_RAW, (Item){.item = *transform_proto_root});
    node_zlib_install_constructor(namespace_root, "InflateRaw", 10,
        node_zlib_create_inflate_raw, NODE_ZLIB_TRANSFORM_INFLATE_RAW, (Item){.item = *transform_proto_root});
    node_zlib_install_constructor(namespace_root, "Unzip", 5,
        node_zlib_create_unzip, NODE_ZLIB_TRANSFORM_UNZIP, (Item){.item = *transform_proto_root});

    *key_root = node_zlib_key("crc32", 5).item;
    *function_root = jube_new_function(node_zlib_host->script, node_zlib_crc32, 2).item;
    node_zlib_host->value->property_set((Item){.item = *namespace_root},
        (Item){.item = *key_root}, (Item){.item = *function_root});
    node_zlib_install_method(namespace_root, key_root, function_root, "gzipSync", 8, node_zlib_gzip_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "gunzipSync", 10, node_zlib_gunzip_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "deflateSync", 11, node_zlib_deflate_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "inflateSync", 11, node_zlib_inflate_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "deflateRawSync", 14, node_zlib_deflate_raw_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "inflateRawSync", 14, node_zlib_inflate_raw_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "unzipSync", 9, node_zlib_unzip_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "gzip", 4, node_zlib_gzip, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "gunzip", 6, node_zlib_gunzip, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "deflate", 7, node_zlib_deflate, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "inflate", 7, node_zlib_inflate, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "deflateRaw", 10, node_zlib_deflate_raw, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "inflateRaw", 10, node_zlib_inflate_raw, 3);
    node_zlib_install_method(namespace_root, key_root, function_root, "unzip", 5, node_zlib_unzip, 3);

    node_zlib_install_method(namespace_root, key_root, function_root, "brotliCompressSync", 18,
        node_zlib_brotli_compress_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "brotliDecompressSync", 20,
        node_zlib_brotli_decompress_sync, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "createGzip", 10,
        node_zlib_create_gzip, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "createGunzip", 12,
        node_zlib_create_gunzip, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "createDeflate", 13,
        node_zlib_create_deflate, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "createInflate", 13,
        node_zlib_create_inflate, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "createDeflateRaw", 16,
        node_zlib_create_deflate_raw, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "createInflateRaw", 16,
        node_zlib_create_inflate_raw, 1);
    node_zlib_install_method(namespace_root, key_root, function_root, "createUnzip", 10,
        node_zlib_create_unzip, 1);

    Item constants = node_zlib_host->value->new_object();
    Item codes = node_zlib_host->value->new_object();
    *constants_root = constants.item;
    *codes_root = codes.item;
#define NODE_ZLIB_SET_CONSTANT(name, constant_value) \
    node_zlib_host->value->property_set(constants, node_zlib_key(name, strlen(name)), \
        (Item){.item = i2it(constant_value)});
    NODE_ZLIB_SET_CONSTANT("Z_NO_FLUSH", 0)
    NODE_ZLIB_SET_CONSTANT("Z_PARTIAL_FLUSH", 1)
    NODE_ZLIB_SET_CONSTANT("Z_SYNC_FLUSH", 2)
    NODE_ZLIB_SET_CONSTANT("Z_FULL_FLUSH", 3)
    NODE_ZLIB_SET_CONSTANT("Z_FINISH", 4)
    NODE_ZLIB_SET_CONSTANT("Z_BLOCK", 5)
    NODE_ZLIB_SET_CONSTANT("Z_TREES", 6)
    NODE_ZLIB_SET_CONSTANT("Z_OK", 0)
    NODE_ZLIB_SET_CONSTANT("Z_STREAM_END", 1)
    NODE_ZLIB_SET_CONSTANT("Z_NEED_DICT", 2)
    NODE_ZLIB_SET_CONSTANT("Z_ERRNO", -1)
    NODE_ZLIB_SET_CONSTANT("Z_STREAM_ERROR", -2)
    NODE_ZLIB_SET_CONSTANT("Z_DATA_ERROR", -3)
    NODE_ZLIB_SET_CONSTANT("Z_MEM_ERROR", -4)
    NODE_ZLIB_SET_CONSTANT("Z_BUF_ERROR", -5)
    NODE_ZLIB_SET_CONSTANT("Z_VERSION_ERROR", -6)
    NODE_ZLIB_SET_CONSTANT("Z_NO_COMPRESSION", 0)
    NODE_ZLIB_SET_CONSTANT("Z_BEST_SPEED", 1)
    NODE_ZLIB_SET_CONSTANT("Z_BEST_COMPRESSION", 9)
    NODE_ZLIB_SET_CONSTANT("Z_DEFAULT_COMPRESSION", -1)
    NODE_ZLIB_SET_CONSTANT("Z_FILTERED", 1)
    NODE_ZLIB_SET_CONSTANT("Z_HUFFMAN_ONLY", 2)
    NODE_ZLIB_SET_CONSTANT("Z_RLE", 3)
    NODE_ZLIB_SET_CONSTANT("Z_FIXED", 4)
    NODE_ZLIB_SET_CONSTANT("Z_DEFAULT_STRATEGY", 0)
    NODE_ZLIB_SET_CONSTANT("Z_MIN_WINDOWBITS", 8)
    NODE_ZLIB_SET_CONSTANT("Z_MAX_WINDOWBITS", 15)
    NODE_ZLIB_SET_CONSTANT("Z_DEFAULT_WINDOWBITS", 15)
    NODE_ZLIB_SET_CONSTANT("Z_MIN_CHUNK", 64)
    NODE_ZLIB_SET_CONSTANT("Z_MAX_CHUNK", INT_MAX)
    NODE_ZLIB_SET_CONSTANT("Z_DEFAULT_CHUNK", 16384)
    NODE_ZLIB_SET_CONSTANT("Z_MIN_MEMLEVEL", 1)
    NODE_ZLIB_SET_CONSTANT("Z_MAX_MEMLEVEL", 9)
    NODE_ZLIB_SET_CONSTANT("Z_DEFAULT_MEMLEVEL", 8)
    NODE_ZLIB_SET_CONSTANT("Z_MIN_LEVEL", -1)
    NODE_ZLIB_SET_CONSTANT("Z_MAX_LEVEL", 9)
    NODE_ZLIB_SET_CONSTANT("DEFLATE", 1)
    NODE_ZLIB_SET_CONSTANT("INFLATE", 2)
    NODE_ZLIB_SET_CONSTANT("GZIP", 3)
    NODE_ZLIB_SET_CONSTANT("GUNZIP", 4)
    NODE_ZLIB_SET_CONSTANT("DEFLATERAW", 5)
    NODE_ZLIB_SET_CONSTANT("INFLATERAW", 6)
    NODE_ZLIB_SET_CONSTANT("UNZIP", 7)
#undef NODE_ZLIB_SET_CONSTANT
    constants = (Item){.item = *constants_root};
    codes = (Item){.item = *codes_root};
    node_zlib_host->script->object_freeze(constants);
    node_zlib_host->value->property_set((Item){.item = *namespace_root},
        node_zlib_key("constants", 9), constants);
    node_zlib_host->script->mark_non_writable((Item){.item = *namespace_root},
        node_zlib_key("constants", 9));

#define NODE_ZLIB_SET_CODE(name, code_value) \
    node_zlib_host->value->property_set(codes, node_zlib_key(name, strlen(name)), \
        (Item){.item = i2it(code_value)});
    NODE_ZLIB_SET_CODE("Z_OK", 0)
    NODE_ZLIB_SET_CODE("Z_STREAM_END", 1)
    NODE_ZLIB_SET_CODE("Z_NEED_DICT", 2)
    NODE_ZLIB_SET_CODE("Z_ERRNO", -1)
    NODE_ZLIB_SET_CODE("Z_STREAM_ERROR", -2)
    NODE_ZLIB_SET_CODE("Z_DATA_ERROR", -3)
    NODE_ZLIB_SET_CODE("Z_MEM_ERROR", -4)
    NODE_ZLIB_SET_CODE("Z_BUF_ERROR", -5)
    NODE_ZLIB_SET_CODE("Z_VERSION_ERROR", -6)
#undef NODE_ZLIB_SET_CODE
    node_zlib_host->script->object_freeze(codes);
    node_zlib_host->value->property_set((Item){.item = *namespace_root},
        node_zlib_key("codes", 5), codes);
    node_zlib_host->script->mark_non_writable((Item){.item = *namespace_root},
        node_zlib_key("codes", 5));
    node_zlib_host->value->property_set((Item){.item = *namespace_root},
        node_zlib_key("default", 7), (Item){.item = *namespace_root});
    node_zlib_host->node->roots->root_frame_end(&frame);
    return (Item){.item = node_zlib_namespace_cache};
}

static int node_zlib_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots || !host->value ||
            !host->script || !host->node->binary || !host->node->error || !host->node->zlib ||
            !host->node->streams ||
            !host->node->async_ops ||
            host->node->binary->struct_size < sizeof(JubeHostBinaryAPI) ||
            host->node->streams->struct_size < sizeof(JubeHostStreamAPI) ||
            host->node->error->struct_size < sizeof(JubeHostNodeErrorAPI) ||
            host->node->zlib->struct_size < sizeof(JubeHostNodeZlibAPI) ||
            host->node->async_ops->struct_size < sizeof(JubeHostAsyncAPI) ||
            !host->node->binary->buffer_alloc || !host->node->binary->buffer_prepare_write ||
            !host->node->binary->buffer_from_bytes ||
            !host->node->error->throw_zlib_error ||
            !host->node->zlib->crc32 || !host->node->zlib->codec || !host->node->zlib->result_release ||
            !host->node->zlib->stream_init || !host->node->zlib->stream_run ||
            !host->node->zlib->stream_free || !host->node->streams->transform_new ||
            !host->node->streams->readable_push || !host->node->streams->transform_flush_drained ||
            !host->node->async_ops->next_tick_callback ||
            !host->script->error_lane_payload ||
            !host->node->streams->transform_prototype) return -1;
    node_zlib_host = host;
    return 0;
}

static void node_zlib_shutdown(void) {
    node_zlib_namespace_cache = 0;
    node_zlib_state_key_cache = 0;
    node_zlib_mode_key_cache = 0;
    node_zlib_transform_prototype_cache = 0;
    for (int i = 0; i < 8; i++) node_zlib_constructor_prototypes[i] = 0;
    node_zlib_session = NULL;
    node_zlib_host = NULL;
}

static void node_zlib_runtime_attach(void* session) {
    node_zlib_session = session;
    if (node_zlib_host && node_zlib_host->node && node_zlib_host->node->roots &&
            node_zlib_host->node->roots->persistent_root_register) {
        (void)node_zlib_host->node->roots->persistent_root_register(session, &node_zlib_namespace_cache);
        (void)node_zlib_host->node->roots->persistent_root_register(session, &node_zlib_state_key_cache);
        (void)node_zlib_host->node->roots->persistent_root_register(session, &node_zlib_mode_key_cache);
        (void)node_zlib_host->node->roots->persistent_root_register(session, &node_zlib_transform_prototype_cache);
        for (int i = 0; i < 8; i++) {
            (void)node_zlib_host->node->roots->persistent_root_register(
                session, &node_zlib_constructor_prototypes[i]);
        }
    }
}
static void node_zlib_runtime_reset(void* session) {
    if (session == node_zlib_session) {
        node_zlib_namespace_cache = 0;
        node_zlib_state_key_cache = 0;
        node_zlib_mode_key_cache = 0;
        node_zlib_transform_prototype_cache = 0;
        for (int i = 0; i < 8; i++) node_zlib_constructor_prototypes[i] = 0;
    }
}
static void node_zlib_runtime_detach(void* session) {
    if (node_zlib_host && node_zlib_host->node && node_zlib_host->node->roots &&
            node_zlib_host->node->roots->persistent_root_unregister) {
        (void)node_zlib_host->node->roots->persistent_root_unregister(session, &node_zlib_namespace_cache);
        (void)node_zlib_host->node->roots->persistent_root_unregister(session, &node_zlib_state_key_cache);
        (void)node_zlib_host->node->roots->persistent_root_unregister(session, &node_zlib_mode_key_cache);
        (void)node_zlib_host->node->roots->persistent_root_unregister(session, &node_zlib_transform_prototype_cache);
        for (int i = 0; i < 8; i++) {
            (void)node_zlib_host->node->roots->persistent_root_unregister(
                session, &node_zlib_constructor_prototypes[i]);
        }
    }
    if (session == node_zlib_session) {
        node_zlib_namespace_cache = 0;
        node_zlib_state_key_cache = 0;
        node_zlib_mode_key_cache = 0;
        node_zlib_transform_prototype_cache = 0;
        for (int i = 0; i < 8; i++) node_zlib_constructor_prototypes[i] = 0;
    }
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
