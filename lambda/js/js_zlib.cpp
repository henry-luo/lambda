/**
 * js_zlib.cpp — Node.js-style 'zlib' module for LambdaJS
 *
 * Provides synchronous gzip/gunzip/deflate/inflate operations.
 * Backed by the zlib library used by the Node zlib module.
 * Registered as built-in module 'zlib' via js_module_get().
 */
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "js_zlib_codec.hpp"

#include <climits>
#include <cstdio>
#include <cstring>
#include <zlib.h>

extern "C" Item js_get_stream_namespace(void);
extern "C" Item js_transform_new(Item opts);
extern "C" Item js_readable_push(Item self, Item chunk);
extern "C" void js_stream_flush_data_if_flowing(Item self);
extern "C" void js_stream_transform_flush_drained(Item self);
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern __thread EvalContext* context;

enum ZlibTransformMode {
    ZLIB_TRANSFORM_GZIP = 1,
    ZLIB_TRANSFORM_GUNZIP,
    ZLIB_TRANSFORM_DEFLATE,
    ZLIB_TRANSFORM_INFLATE,
    ZLIB_TRANSFORM_DEFLATE_RAW,
    ZLIB_TRANSFORM_INFLATE_RAW,
    ZLIB_TRANSFORM_UNZIP
};
JS_FORWARD_STATIC_EXPRESSION(bool, zlib_item_is_undefined, (Item value), (value.item == 0 || value.item == ITEM_JS_UNDEFINED || get_type_id(value) == LMD_TYPE_UNDEFINED))
JS_FORWARD_STATIC_EXPRESSION(bool, zlib_item_is_symbol, (Item value), (get_type_id(value) == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE))

static Item make_zlib_error(const char* method, int zret, const char* detail);
static Item throw_zlib_error(const char* method, int zret, const char* detail);

static bool zlib_bytes_start_gzip_member(const uint8_t* data, int len) {
    if (!data || len <= 0) return false;
    if (data[0] != 0x1f) return false;
    return len == 1 || data[1] == 0x8b;
}

static const char* zlib_error_code_name(int zret) {
    switch (zret) {
    case Z_STREAM_END: return "Z_STREAM_END";
    case Z_NEED_DICT: return "Z_NEED_DICT";
    case Z_ERRNO: return "Z_ERRNO";
    case Z_STREAM_ERROR: return "Z_STREAM_ERROR";
    case Z_DATA_ERROR: return "Z_DATA_ERROR";
    case Z_MEM_ERROR: return "Z_MEM_ERROR";
    case Z_BUF_ERROR: return "Z_BUF_ERROR";
    case Z_VERSION_ERROR: return "Z_VERSION_ERROR";
    default: return "Z_OK";
    }
}

static const char* zlib_error_default_detail(int zret) {
    switch (zret) {
    case Z_NEED_DICT: return "need dictionary";
    case Z_ERRNO: return "zlib errno";
    case Z_STREAM_ERROR: return "stream error";
    case Z_DATA_ERROR: return "data error";
    case Z_MEM_ERROR: return "memory error";
    case Z_BUF_ERROR: return "unexpected end of file";
    case Z_VERSION_ERROR: return "version error";
    default: return "zlib operation failed";
    }
}

// extract buffer data from Uint8Array or string
static bool get_input_buffer(Item input, const uint8_t** out, int* out_len) {
    if (js_is_typed_array(input)) {
        *out = (const uint8_t*)js_typed_array_current_data_ptr(input);
        *out_len = js_typed_array_byte_length(input);
        return *out || *out_len == 0;
    }
    if (js_is_dataview(input)) {
        JsDataView* dv = js_get_dataview_ptr(input);
        if (!dv || !dv->buffer || js_arraybuffer_detached(dv->buffer)) return false;
        int byte_len = dv->length_tracking
            ? js_arraybuffer_length(dv->buffer) - dv->byte_offset
            : dv->byte_length;
        if (byte_len < 0 ||
            dv->byte_offset < 0 ||
            js_arraybuffer_length(dv->buffer) < (int64_t)dv->byte_offset + (int64_t)byte_len) {
            return false;
        }
        const uint8_t* data = js_arraybuffer_data_const(dv->buffer);
        *out = byte_len > 0 ? data + dv->byte_offset : NULL;
        *out_len = byte_len;
        return *out || *out_len == 0;
    }
    if (get_type_id(input) == LMD_TYPE_STRING) {
        String* s = it2s(input);
        *out = (const uint8_t*)s->chars;
        *out_len = (int)s->len;
        return true;
    }
    return false;
}

// create Uint8Array result from raw bytes
static Item make_buffer_result(const uint8_t* data, int len) {
    RootFrame roots(1);
    Rooted<Item> result_root(roots, js_typed_array_new(JS_TYPED_UINT8, len));
    Item result = result_root.get();
    JsTypedArray* ta = js_get_typed_array_ptr(result.map);
    if (ta) {
        ta->is_buffer = true;
        // prepare_write may allocate a writable backing store; retain the view until it owns that store.
        uint8_t* dst = (uint8_t*)js_typed_array_prepare_write_ptr(result_root.get());
        if (dst) memcpy(dst, data, (size_t)len);
    }
    return result_root.get();
}

typedef bool (*NodeZlibSyncCodec)(const uint8_t* data, int length, NodeZlibBytes* out_bytes);

static Item js_zlib_sync_codec(Item input_item, NodeZlibSyncCodec codec, const char* method) {
    RootFrame roots(1);
    Rooted<Item> input_root(roots, input_item);
    const uint8_t* input_data = NULL;
    int input_length = 0;
    if (!codec || !get_input_buffer(input_root.get(), &input_data, &input_length)) {
        log_error("zlib: %sSync: invalid input", method);
        return ItemNull;
    }
    NodeZlibBytes output = {};
    if (!codec(input_data, input_length, &output)) {
        log_error("zlib: %sSync: shared codec failed with %d", method, output.status);
        return throw_zlib_error(method, output.status, NULL);
    }
    Item result = make_buffer_result(output.data, output.length);
    node_zlib_bytes_free(&output);
    return result;
}

#define JS_ZLIB_SYNC_WRAPPER(name, codec, label) \
    extern "C" Item name(Item input_item) { \
        return js_zlib_sync_codec(input_item, codec, label); \
    }
#define JS_ZLIB_SYNC_WRAPPERS(M) \
    M(js_zlib_gzipSync, node_zlib_gzip_encode, "gzip") M(js_zlib_gunzipSync, node_zlib_gunzip_decode, "gunzip") \
    M(js_zlib_unzipSync, node_zlib_unzip_decode, "unzip") M(js_zlib_deflateSync, node_zlib_deflate_encode, "deflate") \
    M(js_zlib_inflateSync, node_zlib_inflate_decode, "inflate") M(js_zlib_deflateRawSync, node_zlib_deflate_raw_encode, "deflateRaw") \
    M(js_zlib_inflateRawSync, node_zlib_inflate_raw_decode, "inflateRaw")
JS_ZLIB_SYNC_WRAPPERS(JS_ZLIB_SYNC_WRAPPER)
#undef JS_ZLIB_SYNC_WRAPPERS
#undef JS_ZLIB_SYNC_WRAPPER

// =============================================================================
// brotliCompressSync / brotliDecompressSync — stubs (need brotli library)
// =============================================================================

extern "C" Item js_zlib_brotliCompressSync(Item input_item) {
    log_error("zlib: brotliCompressSync: brotli not supported");
    return ItemNull;
}

extern "C" Item js_zlib_brotliDecompressSync(Item input_item) {
    log_error("zlib: brotliDecompressSync: brotli not supported");
    return ItemNull;
}

// =============================================================================
// async convenience wrappers — execute locally, report through Node callback form
// =============================================================================

typedef Item (*ZlibSyncFn)(Item);

static Item make_zlib_error(const char* method, int zret, const char* detail) {
    const char* code = zlib_error_code_name(zret);
    const char* reason = detail && detail[0] ? detail : zlib_error_default_detail(zret);
    char msg[256];
    snprintf(msg, sizeof(msg), "%s: %s failed: %s", code, method ? method : "zlib", reason);
    Item error = js_new_error(make_string_item(msg));
    js_set_key_cstr(error, "code", make_string_item(code));
    js_set_key_cstr(error, "errno", (Item){.item = i2it(zret)});
    return error;
}
JS_FORWARD_STATIC_ITEM(throw_zlib_error, (const char* method, int zret, const char* detail), js_throw_value, (make_zlib_error(method, zret, detail)))
JS_FORWARD_ITEM(js_zlib_throw_error_status, (const char* method, int status), throw_zlib_error, (method, status, NULL))

static Item js_zlib_emit_callback(Item env_item) {
    JS_ENV_UNPACK(env, env_item);
    if (!env) return make_js_undefined();

    Item callback = env[0];
    Item err = env[1];
    Item result = env[2];
    if (!is_callable(callback)) return make_js_undefined();

    if (get_type_id(err) != LMD_TYPE_NULL) {
        Item args[1] = { err };
        js_call_function(callback, make_js_undefined(), args, 1);
    } else {
        Item args[2] = { ItemNull, result };
        js_call_function(callback, make_js_undefined(), args, 2);
    }
    return make_js_undefined();
}

static void js_zlib_schedule_callback(Item callback, Item err, Item result) {
    Item values[3] = { callback, err, result };
    JS_TICK_N(js_zlib_emit_callback, 0, values, 3);
}

static Item js_zlib_callback_result(const char* method, ZlibSyncFn sync_fn,
                                    Item input_item, Item options_item, Item callback_item) {
    if (is_callable(options_item) && !is_callable(callback_item)) {
        callback_item = options_item;
    }

    if (!is_callable(callback_item)) {
        return js_throw_invalid_arg_type("callback", "function", callback_item);
    }

    Item result = sync_fn(input_item);
    if (item_is_error(result) || result.item == ItemNull.item) {
        Item err = item_is_error(result) ? js_error_lane_payload(result) :
            make_zlib_error(method, Z_STREAM_ERROR, NULL);
        js_zlib_schedule_callback(callback_item, err, make_js_undefined());
        return make_js_undefined();
    }

    js_zlib_schedule_callback(callback_item, ItemNull, result);
    return make_js_undefined();
}

#define JS_ZLIB_ASYNC_WRAPPER(name, label, sync_fn) \
    extern "C" Item name(Item input_item, Item options_item, Item callback_item) { \
        return js_zlib_callback_result(label, sync_fn, input_item, options_item, callback_item); \
    }
#define JS_ZLIB_ASYNC_WRAPPERS(M) \
    M(js_zlib_gzip, "gzip", js_zlib_gzipSync) M(js_zlib_gunzip, "gunzip", js_zlib_gunzipSync) \
    M(js_zlib_deflate, "deflate", js_zlib_deflateSync) M(js_zlib_inflate, "inflate", js_zlib_inflateSync) \
    M(js_zlib_deflateRaw, "deflateRaw", js_zlib_deflateRawSync) M(js_zlib_inflateRaw, "inflateRaw", js_zlib_inflateRawSync) \
    M(js_zlib_unzip, "unzip", js_zlib_unzipSync)
JS_ZLIB_ASYNC_WRAPPERS(JS_ZLIB_ASYNC_WRAPPER)
#undef JS_ZLIB_ASYNC_WRAPPERS
#undef JS_ZLIB_ASYNC_WRAPPER

// =============================================================================
// createGzip/createGunzip/etc. — Transform-backed one-shot chunk transforms
// =============================================================================

#define zlib_constructor_prototypes (js_runtime_state.zlib.constructor_prototypes)
#define zlib_namespace (js_runtime_state.zlib.namespace_object)
JS_FORWARD_STATIC_EXPRESSION(bool, zlib_ensure_roots, (void), (js_active_runtime_state && js_root_range_ensure_registered(&js_runtime_state.zlib.roots)))

struct JsZlibStreamState {
    z_stream strm;
    int mode;
    int window_bits;
    bool initialized;
    bool finished;
    bool is_deflate;
};
JS_FORWARD_STATIC_EXPRESSION(bool, zlib_mode_is_deflate, (int mode), (mode == ZLIB_TRANSFORM_GZIP || mode == ZLIB_TRANSFORM_DEFLATE || mode == ZLIB_TRANSFORM_DEFLATE_RAW))

static bool zlib_stream_should_reset_member(JsZlibStreamState* state, const uint8_t* data, int len) {
    if (!state || state->is_deflate) return false;
    if (state->mode == ZLIB_TRANSFORM_GUNZIP) return true;
    if (state->mode == ZLIB_TRANSFORM_UNZIP) return zlib_bytes_start_gzip_member(data, len);
    return false;
}

static bool zlib_option_number_value(Item value, double* out_value);

static int zlib_option_int(Item options_item, const char* name, int fallback) {
    if (get_type_id(options_item) != LMD_TYPE_MAP) return fallback;
    Item value = js_get_key_default(options_item, make_string_item(name));
    double number = 0.0;
    // Stream options are user JS Numbers, now boxed as FLOAT even when integral.
    if (!zlib_option_number_value(value, &number)) return fallback;
    if (number != number || number == 1.0 / 0.0 || number == -1.0 / 0.0) return fallback;
    int integer = (int)number;
    if (number != (double)integer) return fallback;
    return integer;
}

static bool zlib_option_number_value(Item value, double* out_value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        *out_value = (double)it2i(value);
        return true;
    }
    if (type == LMD_TYPE_INT64) {
        *out_value = (double)it2l(value);
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        *out_value = it2d(value);
        return true;
    }
    return false;
}

static void zlib_format_number_for_error(Item value, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        snprintf(out, out_size, "%lld", (long long)it2i(value));
        return;
    }
    if (type == LMD_TYPE_INT64) {
        snprintf(out, out_size, "%lld", (long long)it2l(value));
        return;
    }
    if (type == LMD_TYPE_FLOAT) {
        double number = it2d(value);
        if (number != number) snprintf(out, out_size, "NaN");
        else if (number == 1.0 / 0.0) snprintf(out, out_size, "Infinity");
        else if (number == -1.0 / 0.0) snprintf(out, out_size, "-Infinity");
        else if (number == (double)(int64_t)number) snprintf(out, out_size, "%lld", (long long)(int64_t)number);
        else snprintf(out, out_size, "%g", number);
        return;
    }
    snprintf(out, out_size, "undefined");
}

static Item zlib_throw_property_type_error(const char* name, const char* expected, Item actual) {
    char msg[512];
    int pos = snprintf(msg, sizeof(msg),
        "The \"%s\" property must be of type %s.", name, expected);
    if (pos < 0) pos = 0;
    if (pos >= (int)sizeof(msg)) pos = (int)sizeof(msg) - 1;
    if (get_type_id(actual) == LMD_TYPE_STRING) {
        String* s = it2s(actual);
        int len = s ? (int)s->len : 0;
        if (len > 25) len = 25;
        snprintf(msg + pos, sizeof(msg) - (size_t)pos,
            " Received type string ('%.*s%s')", len, s ? s->chars : "",
            (s && s->len > 25) ? "..." : "");
    } else {
        snprintf(msg + pos, sizeof(msg) - (size_t)pos, " Received type %s",
            get_type_id(actual) == LMD_TYPE_BOOL ? "boolean" :
            get_type_id(actual) == LMD_TYPE_NULL ? "null" :
            get_type_id(actual) == LMD_TYPE_UNDEFINED ? "undefined" :
            js_is_callable(actual) ? "function" : "object");
    }
    return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
}

static Item zlib_throw_property_range_error(const char* name, const char* range, Item actual) {
    char actual_buf[64];
    zlib_format_number_for_error(actual, actual_buf, sizeof(actual_buf));
    return js_throw_range_error_codef("ERR_OUT_OF_RANGE", 
        "The value of \"%s\" is out of range. It must be %s. Received %s", name, range, actual_buf);
}

static Item zlib_throw_uint32_range_error(const char* range, Item actual) {
    char actual_buf[64];
    zlib_format_number_for_error(actual, actual_buf, sizeof(actual_buf));
    return js_throw_range_error_codef("ERR_OUT_OF_RANGE", 
        "The value of \"value\" is out of range. It must be %s. Received %s", range, actual_buf);
}

static Item zlib_crc32_seed_value(Item value, uint32_t* out_value) {
    if (zlib_item_is_undefined(value)) {
        *out_value = 0;
        return js_status_ok();
    }
    if (zlib_item_is_symbol(value)) {
        return js_throw_invalid_arg_type("value", "number", value);
    }

    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        int64_t number = it2i(value);
        if (number < 0 || number > 0xFFFFFFFFLL) {
            return zlib_throw_uint32_range_error(">= 0 && <= 4294967295", value);
        }
        *out_value = (uint32_t)number;
        return js_status_ok();
    }
    if (type == LMD_TYPE_INT64) {
        int64_t number = it2l(value);
        if (number < 0 || number > 0xFFFFFFFFLL) {
            return zlib_throw_uint32_range_error(">= 0 && <= 4294967295", value);
        }
        *out_value = (uint32_t)number;
        return js_status_ok();
    }
    if (type == LMD_TYPE_FLOAT) {
        double number = it2d(value);
        if (number != number || number == 1.0 / 0.0 || number == -1.0 / 0.0) {
            return zlib_throw_uint32_range_error("an integer", value);
        }
        if (number < 0.0 || number > 4294967295.0) {
            return zlib_throw_uint32_range_error(">= 0 && <= 4294967295", value);
        }
        uint32_t integer = (uint32_t)number;
        if (number != (double)integer) {
            return zlib_throw_uint32_range_error("an integer", value);
        }
        *out_value = integer;
        return js_status_ok();
    }

    return js_throw_invalid_arg_type("value", "number", value);
}

static bool zlib_validate_int_option(Item options_item, const char* key_name,
                                     const char* prop_name,
                                     int min_value, int max_value, bool allow_zero) {
    if (get_type_id(options_item) != LMD_TYPE_MAP) return true;
    Item value = js_get_key_default(options_item, make_string_item(key_name));
    if (zlib_item_is_undefined(value)) return true;

    double number = 0.0;
    if (!zlib_option_number_value(value, &number)) {
        zlib_throw_property_type_error(prop_name, "number", value);
        return false;
    }
    if (number != number || number == 1.0 / 0.0 || number == -1.0 / 0.0) {
        zlib_throw_property_range_error(prop_name, "a finite number", value);
        return false;
    }
    bool zero_is_allowed = allow_zero && number == 0.0;
    if (!zero_is_allowed && (number < (double)min_value || number > (double)max_value)) {
        char range[64];
        snprintf(range, sizeof(range), ">= %d and <= %d", min_value, max_value);
        zlib_throw_property_range_error(prop_name, range, value);
        return false;
    }
    return true;
}

static bool zlib_validate_stream_options(int mode, Item options_item) {
    if (get_type_id(options_item) != LMD_TYPE_MAP) return true;

    bool is_deflate = zlib_mode_is_deflate(mode);
    int min_window_bits = mode == ZLIB_TRANSFORM_GZIP ? 9 : 8;
    bool allow_zero_window_bits = !is_deflate;
    if (!zlib_validate_int_option(options_item, "windowBits", "options.windowBits",
                                  min_window_bits, 15, allow_zero_window_bits)) {
        return false;
    }
    if (is_deflate) {
        if (!zlib_validate_int_option(options_item, "level", "options.level", -1, 9, false)) return false;
        if (!zlib_validate_int_option(options_item, "memLevel", "options.memLevel", 1, 9, false)) return false;
        if (!zlib_validate_int_option(options_item, "strategy", "options.strategy", 0, 4, false)) return false;
    }
    return true;
}

static int zlib_window_bits_for_mode(int mode, Item options_item) {
    int window_bits = zlib_option_int(options_item, "windowBits", 15);
    if (window_bits <= 0) window_bits = 15;
    switch (mode) {
    case ZLIB_TRANSFORM_GZIP: return window_bits + 16;
    case ZLIB_TRANSFORM_GUNZIP: return window_bits + 16;
    case ZLIB_TRANSFORM_DEFLATE_RAW: return -window_bits;
    case ZLIB_TRANSFORM_INFLATE_RAW: return -window_bits;
    case ZLIB_TRANSFORM_UNZIP: return window_bits + 32;
    default: return window_bits;
    }
}

static JsZlibStreamState* zlib_stream_state_new(int mode, Item options_item) {
    JsZlibStreamState* state = (JsZlibStreamState*)mem_alloc(sizeof(JsZlibStreamState), MEM_CAT_JS_RUNTIME);
    if (!state) return NULL;
    memset(state, 0, sizeof(JsZlibStreamState));
    state->mode = mode;
    state->is_deflate = zlib_mode_is_deflate(mode);

    int ret;
    int window_bits = zlib_window_bits_for_mode(mode, options_item);
    state->window_bits = window_bits;
    if (state->is_deflate) {
        int level = zlib_option_int(options_item, "level", Z_DEFAULT_COMPRESSION);
        int mem_level = zlib_option_int(options_item, "memLevel", 8);
        int strategy = zlib_option_int(options_item, "strategy", Z_DEFAULT_STRATEGY);
        ret = deflateInit2(&state->strm, level, Z_DEFLATED, window_bits, mem_level, strategy);
    } else {
        ret = inflateInit2(&state->strm, window_bits);
    }

    if (ret != Z_OK) {
        mem_free(state);
        return NULL;
    }
    state->initialized = true;
    return state;
}

static int zlib_stream_reset_inflate_member(JsZlibStreamState* state) {
    Bytef* next_in = state->strm.next_in;
    uInt avail_in = state->strm.avail_in;
    int ret = inflateReset2(&state->strm, state->window_bits);
    state->strm.next_in = next_in;
    state->strm.avail_in = avail_in;
    if (ret == Z_OK) state->finished = false;
    return ret;
}

static void zlib_stream_state_close(JsZlibStreamState* state) {
    if (!state || !state->initialized) return;
    if (state->is_deflate) deflateEnd(&state->strm);
    else inflateEnd(&state->strm);
    state->initialized = false;
}

static void zlib_stream_state_free(JsZlibStreamState* state) {
    if (!state) return;
    zlib_stream_state_close(state);
    mem_free(state);
}
JS_FORWARD_STATIC_ITEM(zlib_state_key, (void), make_string_item, ("__zlib_state__"))

static Item zlib_stream_state_item(JsZlibStreamState* state) {
    if (!state) return ItemNull;
    return (Item){.item = i2it((int64_t)(uintptr_t)state)};
}

static JsZlibStreamState* zlib_stream_state_from_item(Item item) {
    if (get_type_id(item) != LMD_TYPE_INT) return NULL;
    return (JsZlibStreamState*)(uintptr_t)it2i(item);
}

static JsZlibStreamState* zlib_stream_state_from_stream(Item stream) {
    return zlib_stream_state_from_item(js_get_key_default(stream, zlib_state_key()));
}

static void zlib_stream_clear_state(Item stream) {
    JsZlibStreamState* state = zlib_stream_state_from_stream(stream);
    js_set_key_default(stream, zlib_state_key(), ItemNull);
    zlib_stream_state_free(state);
}

static bool zlib_stream_run(JsZlibStreamState* state, const uint8_t* in_data,
                            int in_len, int flush, Item* result_out, int* zret_out) {
    if (result_out) *result_out = make_js_undefined();
    if (zret_out) *zret_out = Z_OK;
    if (!state || !state->initialized) {
        if (zret_out) *zret_out = Z_STREAM_ERROR;
        return false;
    }
    if (state->finished) {
        if (in_len > 0 && zlib_stream_should_reset_member(state, in_data, in_len)) {
            int reset_ret = zlib_stream_reset_inflate_member(state);
            if (reset_ret != Z_OK) {
                if (zret_out) *zret_out = reset_ret;
                return false;
            }
        } else if (!state->is_deflate && state->mode == ZLIB_TRANSFORM_UNZIP && in_len > 0) {
            return true;
        } else if (flush == Z_FINISH || in_len == 0) {
            return true;
        } else {
            if (zret_out) *zret_out = Z_STREAM_END;
            return false;
        }
    }

    size_t out_cap = (size_t)in_len * 2 + 16384;
    if (out_cap < 16384) out_cap = 16384;
    uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap, MEM_CAT_JS_RUNTIME);
    if (!out_buf) {
        if (zret_out) *zret_out = Z_MEM_ERROR;
        return false;
    }

    state->strm.next_in = (Bytef*)in_data;
    state->strm.avail_in = (uInt)in_len;

    size_t total_out = 0;
    int ret = Z_OK;
    bool done = false;
    while (!done) {
        if (total_out >= out_cap) {
            out_cap *= 2;
            out_buf = (uint8_t*)mem_realloc(out_buf, out_cap, MEM_CAT_JS_RUNTIME);
            if (!out_buf) {
                if (zret_out) *zret_out = Z_MEM_ERROR;
                return false;
            }
        }

        size_t out_space = out_cap - total_out;
        state->strm.next_out = out_buf + total_out;
        state->strm.avail_out = (uInt)out_space;

        if (state->is_deflate) ret = deflate(&state->strm, flush);
        else ret = inflate(&state->strm, flush);

        total_out += out_space - state->strm.avail_out;

        if (ret == Z_STREAM_END) {
            state->finished = true;
            if (!state->is_deflate && state->strm.avail_in > 0 &&
                zlib_stream_should_reset_member(state, (const uint8_t*)state->strm.next_in,
                                                (int)state->strm.avail_in)) {
                ret = zlib_stream_reset_inflate_member(state);
                if (ret != Z_OK) {
                    if (zret_out) *zret_out = ret;
                    mem_free(out_buf);
                    return false;
                }
            } else if (!state->is_deflate && state->mode == ZLIB_TRANSFORM_UNZIP &&
                       state->strm.avail_in > 0) {
                done = true;
            } else {
                done = true;
            }
        } else if (state->is_deflate) {
            if (ret != Z_OK) {
                if (zret_out) *zret_out = ret;
                mem_free(out_buf);
                return false;
            }
            if (flush == Z_NO_FLUSH) {
                done = state->strm.avail_in == 0 && state->strm.avail_out != 0;
            } else if (flush != Z_FINISH) {
                done = state->strm.avail_out != 0;
            }
        } else {
            if (ret == Z_BUF_ERROR && flush != Z_FINISH) {
                ret = Z_OK;
                done = true;
            } else if (ret != Z_OK) {
                if (zret_out) *zret_out = ret;
                mem_free(out_buf);
                return false;
            } else if (flush == Z_NO_FLUSH) {
                done = state->strm.avail_in == 0 && state->strm.avail_out != 0;
            } else if (flush != Z_FINISH) {
                done = state->strm.avail_out != 0;
            } else if (state->strm.avail_in == 0 && state->strm.avail_out != 0) {
                ret = Z_BUF_ERROR;
                if (zret_out) *zret_out = ret;
                mem_free(out_buf);
                return false;
            }
        }
    }

    if (zret_out) *zret_out = ret;
    if (total_out > 0 && result_out) {
        *result_out = make_buffer_result(out_buf, (int)total_out);
    }
    mem_free(out_buf);
    return true;
}

static const char* zlib_mode_name(int mode) {
    switch (mode) {
    case ZLIB_TRANSFORM_GZIP: return "Gzip";
    case ZLIB_TRANSFORM_GUNZIP: return "Gunzip";
    case ZLIB_TRANSFORM_DEFLATE: return "Deflate";
    case ZLIB_TRANSFORM_INFLATE: return "Inflate";
    case ZLIB_TRANSFORM_DEFLATE_RAW: return "DeflateRaw";
    case ZLIB_TRANSFORM_INFLATE_RAW: return "InflateRaw";
    case ZLIB_TRANSFORM_UNZIP: return "Unzip";
    default: return "Zlib";
    }
}

static Item js_zlib_transform_chunk(Item chunk, Item encoding, Item callback) {
    (void)encoding;
    Item self = js_get_this();
    Item mode_item = js_get_key_cstr(self, "__zlib_mode__");
    int mode = get_type_id(mode_item) == LMD_TYPE_INT ? (int)it2i(mode_item) : 0;
    JsZlibStreamState* state = zlib_stream_state_from_stream(self);
    if (!state) {
        Item args[1] = { js_new_error(make_string_item("zlib transform mode missing")) };
        if (is_callable(callback)) js_call_function(callback, make_js_undefined(), args, 1);
        return make_js_undefined();
    }

    const uint8_t* in_data;
    int in_len;
    if (!get_input_buffer(chunk, &in_data, &in_len)) {
        Item args[1] = { make_zlib_error(zlib_mode_name(mode), Z_STREAM_ERROR, "invalid input") };
        if (is_callable(callback)) js_call_function(callback, make_js_undefined(), args, 1);
        return make_js_undefined();
    }

    Item result = make_js_undefined();
    int zret = Z_OK;
    if (!zlib_stream_run(state, in_data, in_len, Z_NO_FLUSH, &result, &zret)) {
        Item args[1] = { make_zlib_error(zlib_mode_name(mode), zret, NULL) };
        if (is_callable(callback)) js_call_function(callback, make_js_undefined(), args, 1);
        return make_js_undefined();
    }

    if (is_callable(callback)) {
        if (get_type_id(result) == LMD_TYPE_UNDEFINED) {
            Item args[1] = { ItemNull };
            js_call_function(callback, make_js_undefined(), args, 1);
        } else {
            Item args[2] = { ItemNull, result };
            js_call_function(callback, make_js_undefined(), args, 2);
        }
    }
    return make_js_undefined();
}

static Item js_zlib_transform_flush(Item callback) {
    Item self = js_get_this();
    Item mode_item = js_get_key_cstr(self, "__zlib_mode__");
    int mode = get_type_id(mode_item) == LMD_TYPE_INT ? (int)it2i(mode_item) : 0;
    JsZlibStreamState* state = zlib_stream_state_from_stream(self);
    if (!state) {
        Item args[1] = { make_zlib_error(zlib_mode_name(mode), Z_STREAM_ERROR, NULL) };
        if (is_callable(callback)) js_call_function(callback, make_js_undefined(), args, 1);
        return make_js_undefined();
    }

    Item result = make_js_undefined();
    int zret = Z_OK;
    bool ok = zlib_stream_run(state, NULL, 0, Z_FINISH, &result, &zret);
    if (ok && get_type_id(result) != LMD_TYPE_UNDEFINED) {
        js_readable_push(self, result);
        js_stream_flush_data_if_flowing(self);
        // zlib flush exposes pending compressed bytes; drain the writable side
        // only after those bytes have reached the readable consumer.
        js_stream_transform_flush_drained(self);
    }
    zlib_stream_clear_state(self);

    if (!ok) {
        Item args[1] = { make_zlib_error(zlib_mode_name(mode), zret, NULL) };
        if (is_callable(callback)) js_call_function(callback, make_js_undefined(), args, 1);
        return make_js_undefined();
    }

    if (is_callable(callback)) js_call_function(callback, make_js_undefined(), NULL, 0);
    return make_js_undefined();
}

static Item js_zlib_transform_destroy(Item err, Item callback) {
    Item self = js_get_this();
    zlib_stream_clear_state(self);
    if (is_callable(callback)) {
        if (err.item != 0 && get_type_id(err) != LMD_TYPE_UNDEFINED &&
            get_type_id(err) != LMD_TYPE_NULL) {
            js_call_function(callback, make_js_undefined(), &err, 1);
        } else {
            Item args[1] = { ItemNull };
            js_call_function(callback, make_js_undefined(), args, 1);
        }
    }
    return make_js_undefined();
}

static Item js_zlib_stream_flush_method(Item kind_item, Item callback_item) {
    Item self = js_get_this();
    int flush = Z_FULL_FLUSH;
    if (is_callable(kind_item) &&
        (callback_item.item == 0 || get_type_id(callback_item) == LMD_TYPE_UNDEFINED)) {
        callback_item = kind_item;
    } else {
        double kind_number = 0.0;
        // flush(kind) receives normal JS Numbers, which are boxed FLOAT after migration.
        if (zlib_option_number_value(kind_item, &kind_number) &&
            kind_number == (double)(int)kind_number) {
            flush = (int)kind_number;
        }
    }

    Item mode_item = js_get_key_cstr(self, "__zlib_mode__");
    int mode = get_type_id(mode_item) == LMD_TYPE_INT ? (int)it2i(mode_item) : 0;
    JsZlibStreamState* state = zlib_stream_state_from_stream(self);
    if (!state) {
        Item args[1] = { make_zlib_error(zlib_mode_name(mode), Z_STREAM_ERROR, NULL) };
        if (is_callable(callback_item)) js_call_function(callback_item, make_js_undefined(), args, 1);
        return make_js_undefined();
    }

    Item result = make_js_undefined();
    int zret = Z_OK;
    bool ok = zlib_stream_run(state, NULL, 0, flush, &result, &zret);
    if (ok && get_type_id(result) != LMD_TYPE_UNDEFINED) {
        js_readable_push(self, result);
        js_stream_flush_data_if_flowing(self);
        // zlib manual flush releases data that was holding transform backpressure.
        js_stream_transform_flush_drained(self);
    }

    if (!ok) {
        Item args[1] = { make_zlib_error(zlib_mode_name(mode), zret, NULL) };
        if (is_callable(callback_item)) js_call_function(callback_item, make_js_undefined(), args, 1);
        return make_js_undefined();
    }

    if (is_callable(callback_item)) js_call_function(callback_item, make_js_undefined(), NULL, 0);
    return make_js_undefined();
}

static Item js_zlib_create_transform(int mode, Item options_item) {
    (void)js_get_stream_namespace();
    if (!zlib_validate_stream_options(mode, options_item)) return ItemNull;

    Item stream = js_transform_new(options_item);
    if (stream.item == ItemNull.item) return stream;

    JsZlibStreamState* state = zlib_stream_state_new(mode, options_item);
    if (!state) return ItemNull;

    js_set_key_cstr(stream, "__zlib_mode__", (Item){.item = i2it(mode)});
    js_set_key_default(stream, zlib_state_key(), zlib_stream_state_item(state));
    js_mark_non_enumerable(stream, zlib_state_key());
    js_set_native_key(stream, make_string_item("_transform"), js_zlib_transform_chunk);
    js_set_native_key(stream, make_string_item("_flush"), js_zlib_transform_flush);
    js_set_native_key(stream, make_string_item("_destroy"), js_zlib_transform_destroy);
    js_set_native_key(stream, make_string_item("flush"), js_zlib_stream_flush_method);

    if (mode >= ZLIB_TRANSFORM_GZIP && mode <= ZLIB_TRANSFORM_UNZIP) {
        Item proto = zlib_constructor_prototypes[mode];
        if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(stream, proto);
    }
    return stream;
}

#define JS_ZLIB_TRANSFORM_WRAPPER(name, mode) \
    extern "C" Item name(Item options_item) { \
        return js_zlib_create_transform(mode, options_item); \
    }
#define JS_ZLIB_TRANSFORM_WRAPPERS(M) \
    M(js_zlib_createGzip, ZLIB_TRANSFORM_GZIP) M(js_zlib_createGunzip, ZLIB_TRANSFORM_GUNZIP) \
    M(js_zlib_createDeflate, ZLIB_TRANSFORM_DEFLATE) M(js_zlib_createInflate, ZLIB_TRANSFORM_INFLATE) \
    M(js_zlib_createDeflateRaw, ZLIB_TRANSFORM_DEFLATE_RAW) M(js_zlib_createInflateRaw, ZLIB_TRANSFORM_INFLATE_RAW) \
    M(js_zlib_createUnzip, ZLIB_TRANSFORM_UNZIP)
JS_ZLIB_TRANSFORM_WRAPPERS(JS_ZLIB_TRANSFORM_WRAPPER)
#undef JS_ZLIB_TRANSFORM_WRAPPERS
#undef JS_ZLIB_TRANSFORM_WRAPPER

// =============================================================================
// zlib Module Namespace
// =============================================================================

// crc32(data[, value]) — compute CRC32
extern "C" Item js_zlib_crc32(Item data_item, Item init_val) {
    uint32_t crc_val = 0;
    JS_RETURN_IF_ERROR(zlib_crc32_seed_value(init_val, &crc_val));

    const uint8_t* data = NULL;
    int data_len = 0;
    if (!get_input_buffer(data_item, &data, &data_len)) {
        return js_throw_invalid_arg_type("data", "string, Buffer, TypedArray, or DataView", data_item);
    }
    crc_val = node_zlib_crc32_bytes(data, data_len, crc_val);

    // return as an unsigned 32-bit value represented by Lambda's signed int slot.
    return (Item){.item = i2it((int64_t)crc_val)};
}

template <typename Target>
JS_FORWARD_STATIC_VOID( zlib_set_method, (Item ns, const char* name, Target target,         int adapter_arity), js_install_native_method, (ns, name, target, adapter_arity))

template <typename Target>
static Item zlib_set_constructor(Item ns, const char* name, Target target,
        int mode, Item transform_proto) {
    RootFrame roots(4);
    Rooted<Item> ns_root(roots, ns);
    Rooted<Item> transform_proto_root(roots, transform_proto);
    Rooted<Item> ctor_root(roots, js_new_native_constructor(target));
    Rooted<Item> proto_root(roots, js_new_object());
    // Constructor and prototype are mutually linked before either is
    // published in the namespace, so both need exact construction roots.
    if (get_type_id(transform_proto_root.get()) == LMD_TYPE_MAP) {
        js_set_prototype(proto_root.get(), transform_proto_root.get());
    }
    js_set_key_cstr(proto_root.get(), "constructor", ctor_root.get());
    js_mark_non_enumerable(proto_root.get(), make_string_item("constructor"));
    js_set_key_cstr(ctor_root.get(), "prototype", proto_root.get());
    js_function_set_prototype(ctor_root.get(), proto_root.get());
    js_set_function_name(ctor_root.get(), make_string_item(name));
    if (mode >= ZLIB_TRANSFORM_GZIP && mode <= ZLIB_TRANSFORM_UNZIP) {
        zlib_constructor_prototypes[mode] = proto_root.get();
    }
    js_set_key_default(ns_root.get(), make_string_item(name), ctor_root.get());
    return ctor_root.get();
}

extern "C" Item js_get_zlib_namespace(void) {
    if (!zlib_ensure_roots()) return ItemError;
    if (zlib_namespace.item != 0) return zlib_namespace;

    zlib_namespace = js_new_object();
    JS_ROOTS(roots,
        ns_root, zlib_namespace,
        stream_root, ItemNull,
        transform_ctor_root, ItemNull,
        transform_proto_root, ItemNull,
        constants_root, ItemNull,
        codes_root, ItemNull);
    // The namespace is persistent, while stream-derived prototypes and the
    // two frozen tables remain unpublished during allocating initialization.

    stream_root.set(js_get_stream_namespace());
    transform_ctor_root.set(js_get_key_cstr(stream_root.get(), "Transform"));
    transform_proto_root.set(js_get_key_cstr(transform_ctor_root.get(), "prototype"));

    zlib_set_constructor(ns_root.get(), "Gzip",       js_zlib_createGzip,
                         ZLIB_TRANSFORM_GZIP, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "Gunzip",     js_zlib_createGunzip,
                         ZLIB_TRANSFORM_GUNZIP, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "Deflate",    js_zlib_createDeflate,
                         ZLIB_TRANSFORM_DEFLATE, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "Inflate",    js_zlib_createInflate,
                         ZLIB_TRANSFORM_INFLATE, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "DeflateRaw", js_zlib_createDeflateRaw,
                         ZLIB_TRANSFORM_DEFLATE_RAW, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "InflateRaw", js_zlib_createInflateRaw,
                         ZLIB_TRANSFORM_INFLATE_RAW, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "Unzip",      js_zlib_createUnzip,
                         ZLIB_TRANSFORM_UNZIP, transform_proto_root.get());

#define JS_ZLIB_INSTALL_METHOD(name, target, arity) \
    zlib_set_method(zlib_namespace, name, target, arity);
#define JS_ZLIB_METHODS(M) \
    M("gzip", js_zlib_gzip, 3) M("gunzip", js_zlib_gunzip, 3) \
    M("deflate", js_zlib_deflate, 3) M("inflate", js_zlib_inflate, 3) \
    M("deflateRaw", js_zlib_deflateRaw, 3) M("inflateRaw", js_zlib_inflateRaw, 3) \
    M("unzip", js_zlib_unzip, 3) M("gzipSync", js_zlib_gzipSync, 1) \
    M("gunzipSync", js_zlib_gunzipSync, 1) M("deflateSync", js_zlib_deflateSync, 1) \
    M("inflateSync", js_zlib_inflateSync, 1) M("deflateRawSync", js_zlib_deflateRawSync, 1) \
    M("inflateRawSync", js_zlib_inflateRawSync, 1) M("brotliCompressSync", js_zlib_brotliCompressSync, 1) \
    M("brotliDecompressSync", js_zlib_brotliDecompressSync, 1) M("unzipSync", js_zlib_unzipSync, 1) \
    M("crc32", js_zlib_crc32, 2) M("createGzip", js_zlib_createGzip, 1) \
    M("createGunzip", js_zlib_createGunzip, 1) M("createDeflate", js_zlib_createDeflate, 1) \
    M("createInflate", js_zlib_createInflate, 1) M("createDeflateRaw", js_zlib_createDeflateRaw, 1) \
    M("createInflateRaw", js_zlib_createInflateRaw, 1) M("createUnzip", js_zlib_createUnzip, 1)
JS_ZLIB_METHODS(JS_ZLIB_INSTALL_METHOD)
#undef JS_ZLIB_METHODS
#undef JS_ZLIB_INSTALL_METHOD

    // constants — all zlib constants including flush modes, error codes, compression levels, strategies
    Item constants = js_new_object();
    constants_root.set(constants);
#define JS_ZLIB_ERROR_CODES(M) \
    M("Z_OK", Z_OK) M("Z_STREAM_END", Z_STREAM_END) M("Z_NEED_DICT", Z_NEED_DICT) \
    M("Z_ERRNO", Z_ERRNO) M("Z_STREAM_ERROR", Z_STREAM_ERROR) M("Z_DATA_ERROR", Z_DATA_ERROR) \
    M("Z_MEM_ERROR", Z_MEM_ERROR) M("Z_BUF_ERROR", Z_BUF_ERROR) M("Z_VERSION_ERROR", Z_VERSION_ERROR)
#define JS_ZLIB_CONSTANTS(M) \
    M("Z_NO_FLUSH", Z_NO_FLUSH) M("Z_PARTIAL_FLUSH", Z_PARTIAL_FLUSH) \
    M("Z_SYNC_FLUSH", Z_SYNC_FLUSH) M("Z_FULL_FLUSH", Z_FULL_FLUSH) \
    M("Z_FINISH", Z_FINISH) M("Z_BLOCK", Z_BLOCK) M("Z_TREES", Z_TREES) \
    JS_ZLIB_ERROR_CODES(M) M("Z_NO_COMPRESSION", Z_NO_COMPRESSION) \
    M("Z_BEST_SPEED", Z_BEST_SPEED) M("Z_BEST_COMPRESSION", Z_BEST_COMPRESSION) \
    M("Z_DEFAULT_COMPRESSION", Z_DEFAULT_COMPRESSION) M("Z_FILTERED", Z_FILTERED) \
    M("Z_HUFFMAN_ONLY", Z_HUFFMAN_ONLY) M("Z_RLE", Z_RLE) M("Z_FIXED", Z_FIXED) \
    M("Z_DEFAULT_STRATEGY", Z_DEFAULT_STRATEGY) M("Z_MIN_WINDOWBITS", 8) \
    M("Z_MAX_WINDOWBITS", 15) M("Z_DEFAULT_WINDOWBITS", 15) M("Z_MIN_CHUNK", 64) \
    M("Z_MAX_CHUNK", INT_MAX) M("Z_DEFAULT_CHUNK", 16384) M("Z_MIN_MEMLEVEL", 1) \
    M("Z_MAX_MEMLEVEL", 9) M("Z_DEFAULT_MEMLEVEL", 8) M("Z_MIN_LEVEL", -1) \
    M("Z_MAX_LEVEL", 9) M("DEFLATE", 1) M("INFLATE", 2) M("GZIP", 3) \
    M("GUNZIP", 4) M("DEFLATERAW", 5) M("INFLATERAW", 6) M("UNZIP", 7)
#define JS_ZLIB_SET_CONSTANT(name, value) \
    js_set_key_cstr(constants, name, (Item){.item = i2it(value)});
    JS_ZLIB_CONSTANTS(JS_ZLIB_SET_CONSTANT)
#undef JS_ZLIB_SET_CONSTANT
    js_object_freeze(constants);
    Item constants_key = make_string_item("constants");
    js_set_key_default(zlib_namespace, constants_key, constants);
    js_mark_non_writable(zlib_namespace, constants_key);
    js_mark_non_configurable(zlib_namespace, constants_key);

    // codes — error code map (frozen)
    Item codes = js_new_object();
    codes_root.set(codes);
#define JS_ZLIB_SET_CODE(name, value) \
    js_set_key_cstr(codes, name, (Item){.item = i2it(value)});
    JS_ZLIB_ERROR_CODES(JS_ZLIB_SET_CODE)
#undef JS_ZLIB_SET_CODE
#undef JS_ZLIB_CONSTANTS
#undef JS_ZLIB_ERROR_CODES
    js_object_freeze(codes);
    Item codes_key = make_string_item("codes");
    js_set_key_default(zlib_namespace, codes_key, codes);
    js_mark_non_writable(zlib_namespace, codes_key);
    js_mark_non_configurable(zlib_namespace, codes_key);

    Item default_key = make_string_item("default");
    js_set_key_default(zlib_namespace, default_key, zlib_namespace);

    return zlib_namespace;
}

extern "C" void js_zlib_reset(void) {
    if (!js_active_runtime_state) return;
    zlib_namespace = (Item){0};
    for (int i = 0; i < 8; i++) zlib_constructor_prototypes[i] = (Item){0};
}
