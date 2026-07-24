/**
 * js_zlib.cpp — Node.js-style 'zlib' module for LambdaJS
 *
 * Provides synchronous gzip/gunzip/deflate/inflate operations.
 * Backed by zlib (already linked for npm tarball extraction).
 * Registered as built-in module 'zlib' via js_module_get().
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../runtime/transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <zlib.h>

extern "C" Item js_get_stream_namespace(void);
extern "C" Item js_transform_new(Item opts);
extern "C" void js_function_set_prototype(Item fn_item, Item proto);
extern "C" Item js_readable_push(Item self, Item chunk);
extern "C" void js_stream_flush_data_if_flowing(Item self);
extern "C" void js_stream_transform_flush_drained(Item self);
extern "C" void js_next_tick_enqueue(Item callback);
extern "C" void js_mark_non_writable(Item object, Item name);
extern "C" void js_mark_non_configurable(Item object, Item name);
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

static bool zlib_item_is_undefined(Item value) {
    return value.item == 0 || value.item == ITEM_JS_UNDEFINED ||
           get_type_id(value) == LMD_TYPE_UNDEFINED;
}

static bool zlib_item_is_symbol(Item value) {
    return get_type_id(value) == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE;
}

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
    Item result = js_typed_array_new(JS_TYPED_UINT8, len);
    JsTypedArray* ta = js_get_typed_array_ptr(result.map);
    if (ta) {
        ta->is_buffer = true;
        uint8_t* dst = (uint8_t*)js_typed_array_prepare_write_ptr(result);
        if (dst) memcpy(dst, data, (size_t)len);
    }
    return result;
}

// =============================================================================
// gzipSync(buffer) — compress with gzip
// =============================================================================

extern "C" Item js_zlib_gzipSync(Item input_item) {
    const uint8_t* in_data;
    int in_len;
    if (!get_input_buffer(input_item, &in_data, &in_len)) {
        log_error("zlib: gzipSync: invalid input");
        return ItemNull;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    // windowBits = 15 + 16 for gzip encoding
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        log_error("zlib: gzipSync: deflateInit2 failed");
        return ItemNull;
    }

    size_t out_cap = compressBound((uLong)in_len) + 32;
    uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap, MEM_CAT_JS_RUNTIME);

    strm.next_in = (Bytef*)in_data;
    strm.avail_in = (uInt)in_len;
    strm.next_out = out_buf;
    strm.avail_out = (uInt)out_cap;

    int ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        log_error("zlib: gzipSync: deflate failed with %d", ret);
        deflateEnd(&strm);
        mem_free(out_buf);
        return throw_zlib_error("gzip", ret, NULL);
    }
    deflateEnd(&strm);

    int out_len = (int)strm.total_out;
    Item result = make_buffer_result(out_buf, out_len);
    mem_free(out_buf);
    return result;
}

// =============================================================================
// gunzipSync(buffer) — decompress gzip
// =============================================================================

extern "C" Item js_zlib_gunzipSync(Item input_item) {
    const uint8_t* in_data;
    int in_len;
    if (!get_input_buffer(input_item, &in_data, &in_len)) {
        log_error("zlib: gunzipSync: invalid input");
        return ItemNull;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    // windowBits = 15 + 16 for gzip decoding
    if (inflateInit2(&strm, 15 + 16) != Z_OK) {
        log_error("zlib: gunzipSync: inflateInit2 failed");
        return ItemNull;
    }

    size_t out_cap = (size_t)in_len * 4;
    if (out_cap < 4096) out_cap = 4096;
    uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap, MEM_CAT_JS_RUNTIME);

    strm.next_in = (Bytef*)in_data;
    strm.avail_in = (uInt)in_len;

    size_t total_out = 0;
    int ret;
    while (true) {
        if (total_out >= out_cap) {
            out_cap *= 2;
            out_buf = (uint8_t*)mem_realloc(out_buf, out_cap, MEM_CAT_JS_RUNTIME);
        }
        size_t out_space = out_cap - total_out;
        strm.next_out = out_buf + total_out;
        strm.avail_out = (uInt)out_space;
        ret = inflate(&strm, Z_NO_FLUSH);
        total_out += out_space - strm.avail_out;
        if (ret == Z_STREAM_END) {
            if (strm.avail_in > 0) {
                Bytef* next_in = strm.next_in;
                uInt avail_in = strm.avail_in;
                ret = inflateReset2(&strm, 15 + 16);
                if (ret != Z_OK) break;
                strm.next_in = next_in;
                strm.avail_in = avail_in;
                continue;
            }
            break;
        }
        if (ret != Z_OK) break;
    }

    char detail[128];
    detail[0] = '\0';
    if (ret != Z_STREAM_END && strm.msg) snprintf(detail, sizeof(detail), "%s", strm.msg);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        log_error("zlib: gunzipSync: inflate failed with %d", ret);
        mem_free(out_buf);
        return throw_zlib_error("gunzip", ret, detail);
    }

    Item result = make_buffer_result(out_buf, (int)total_out);
    mem_free(out_buf);
    return result;
}

// =============================================================================
// unzipSync(buffer) — auto-detect gzip or zlib-wrapped deflate
// =============================================================================

extern "C" Item js_zlib_unzipSync(Item input_item) {
    const uint8_t* in_data;
    int in_len;
    if (!get_input_buffer(input_item, &in_data, &in_len)) {
        log_error("zlib: unzipSync: invalid input");
        return ItemNull;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    // windowBits = 15 + 32 asks zlib to auto-detect gzip or zlib headers.
    if (inflateInit2(&strm, 15 + 32) != Z_OK) {
        log_error("zlib: unzipSync: inflateInit2 failed");
        return ItemNull;
    }

    size_t out_cap = (size_t)in_len * 4;
    if (out_cap < 4096) out_cap = 4096;
    uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap, MEM_CAT_JS_RUNTIME);

    strm.next_in = (Bytef*)in_data;
    strm.avail_in = (uInt)in_len;

    size_t total_out = 0;
    int ret;
    while (true) {
        if (total_out >= out_cap) {
            out_cap *= 2;
            out_buf = (uint8_t*)mem_realloc(out_buf, out_cap, MEM_CAT_JS_RUNTIME);
        }
        size_t out_space = out_cap - total_out;
        strm.next_out = out_buf + total_out;
        strm.avail_out = (uInt)out_space;
        ret = inflate(&strm, Z_NO_FLUSH);
        total_out += out_space - strm.avail_out;
        if (ret == Z_STREAM_END) {
            if (strm.avail_in > 0 &&
                zlib_bytes_start_gzip_member((const uint8_t*)strm.next_in, (int)strm.avail_in)) {
                Bytef* next_in = strm.next_in;
                uInt avail_in = strm.avail_in;
                ret = inflateReset2(&strm, 15 + 32);
                if (ret != Z_OK) break;
                strm.next_in = next_in;
                strm.avail_in = avail_in;
                continue;
            }
            break;
        }
        if (ret != Z_OK) break;
    }

    char detail[128];
    detail[0] = '\0';
    if (ret != Z_STREAM_END && strm.msg) snprintf(detail, sizeof(detail), "%s", strm.msg);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        log_error("zlib: unzipSync: inflate failed with %d", ret);
        mem_free(out_buf);
        return throw_zlib_error("unzip", ret, detail);
    }

    Item result = make_buffer_result(out_buf, (int)total_out);
    mem_free(out_buf);
    return result;
}

// =============================================================================
// deflateSync(buffer) — raw deflate (no gzip header)
// =============================================================================

extern "C" Item js_zlib_deflateSync(Item input_item) {
    const uint8_t* in_data;
    int in_len;
    if (!get_input_buffer(input_item, &in_data, &in_len)) {
        log_error("zlib: deflateSync: invalid input");
        return ItemNull;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
        log_error("zlib: deflateSync: deflateInit failed");
        return ItemNull;
    }

    size_t out_cap = compressBound((uLong)in_len) + 32;
    uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap, MEM_CAT_JS_RUNTIME);

    strm.next_in = (Bytef*)in_data;
    strm.avail_in = (uInt)in_len;
    strm.next_out = out_buf;
    strm.avail_out = (uInt)out_cap;

    int ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        mem_free(out_buf);
        return throw_zlib_error("deflate", ret, NULL);
    }
    deflateEnd(&strm);

    Item result = make_buffer_result(out_buf, (int)strm.total_out);
    mem_free(out_buf);
    return result;
}

// =============================================================================
// inflateSync(buffer) — raw inflate
// =============================================================================

extern "C" Item js_zlib_inflateSync(Item input_item) {
    const uint8_t* in_data;
    int in_len;
    if (!get_input_buffer(input_item, &in_data, &in_len)) {
        log_error("zlib: inflateSync: invalid input");
        return ItemNull;
    }

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit(&strm) != Z_OK) {
        log_error("zlib: inflateSync: inflateInit failed");
        return ItemNull;
    }

    size_t out_cap = (size_t)in_len * 4;
    if (out_cap < 4096) out_cap = 4096;
    uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap, MEM_CAT_JS_RUNTIME);

    strm.next_in = (Bytef*)in_data;
    strm.avail_in = (uInt)in_len;

    size_t total_out = 0;
    int ret;
    do {
        if (total_out >= out_cap) {
            out_cap *= 2;
            out_buf = (uint8_t*)mem_realloc(out_buf, out_cap, MEM_CAT_JS_RUNTIME);
        }
        strm.next_out = out_buf + total_out;
        strm.avail_out = (uInt)(out_cap - total_out);
        ret = inflate(&strm, Z_NO_FLUSH);
        total_out = strm.total_out;
    } while (ret == Z_OK);

    char detail[128];
    detail[0] = '\0';
    if (ret != Z_STREAM_END && strm.msg) snprintf(detail, sizeof(detail), "%s", strm.msg);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        mem_free(out_buf);
        return throw_zlib_error("inflate", ret, detail);
    }

    Item result = make_buffer_result(out_buf, (int)total_out);
    mem_free(out_buf);
    return result;
}

// =============================================================================
// deflateRawSync(buffer) — raw deflate with windowBits = -15
// =============================================================================

extern "C" Item js_zlib_deflateRawSync(Item input_item) {
    const uint8_t* in_data;
    int in_len;
    if (!get_input_buffer(input_item, &in_data, &in_len)) return ItemNull;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return ItemNull;

    size_t out_cap = compressBound((uLong)in_len) + 32;
    uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap, MEM_CAT_JS_RUNTIME);

    strm.next_in = (Bytef*)in_data;
    strm.avail_in = (uInt)in_len;
    strm.next_out = out_buf;
    strm.avail_out = (uInt)out_cap;

    int ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        mem_free(out_buf);
        return throw_zlib_error("deflateRaw", ret, NULL);
    }
    deflateEnd(&strm);

    Item result = make_buffer_result(out_buf, (int)strm.total_out);
    mem_free(out_buf);
    return result;
}

// =============================================================================
// inflateRawSync(buffer) — raw inflate with windowBits = -15
// =============================================================================

extern "C" Item js_zlib_inflateRawSync(Item input_item) {
    const uint8_t* in_data;
    int in_len;
    if (!get_input_buffer(input_item, &in_data, &in_len)) return ItemNull;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -15) != Z_OK) return ItemNull;

    size_t out_cap = (size_t)in_len * 4;
    if (out_cap < 4096) out_cap = 4096;
    uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap, MEM_CAT_JS_RUNTIME);

    strm.next_in = (Bytef*)in_data;
    strm.avail_in = (uInt)in_len;

    size_t total_out = 0;
    int ret;
    do {
        if (total_out >= out_cap) {
            out_cap *= 2;
            out_buf = (uint8_t*)mem_realloc(out_buf, out_cap, MEM_CAT_JS_RUNTIME);
        }
        strm.next_out = out_buf + total_out;
        strm.avail_out = (uInt)(out_cap - total_out);
        ret = inflate(&strm, Z_NO_FLUSH);
        total_out = strm.total_out;
    } while (ret == Z_OK);

    char detail[128];
    detail[0] = '\0';
    if (ret != Z_STREAM_END && strm.msg) snprintf(detail, sizeof(detail), "%s", strm.msg);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        mem_free(out_buf);
        return throw_zlib_error("inflateRaw", ret, detail);
    }

    Item result = make_buffer_result(out_buf, (int)total_out);
    mem_free(out_buf);
    return result;
}

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
    js_property_set(error, make_string_item("code"), make_string_item(code));
    js_property_set(error, make_string_item("errno"), (Item){.item = i2it(zret)});
    return error;
}

static Item throw_zlib_error(const char* method, int zret, const char* detail) {
    js_throw_value(make_zlib_error(method, zret, detail));
    return ItemNull;
}

static Item js_zlib_emit_callback(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
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
    Item* env = js_alloc_env(3);
    env[0] = callback;
    env[1] = err;
    env[2] = result;
    js_next_tick_enqueue(js_new_closure((void*)js_zlib_emit_callback, 0, env, 3));
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
    if (result.item == ItemNull.item) {
        Item err = js_check_exception() ? js_clear_exception() :
            make_zlib_error(method, Z_STREAM_ERROR, NULL);
        js_zlib_schedule_callback(callback_item, err, make_js_undefined());
        return make_js_undefined();
    }

    js_zlib_schedule_callback(callback_item, ItemNull, result);
    return make_js_undefined();
}

extern "C" Item js_zlib_gzip(Item input_item, Item options_item, Item callback_item) {
    return js_zlib_callback_result("gzip", js_zlib_gzipSync, input_item, options_item, callback_item);
}

extern "C" Item js_zlib_gunzip(Item input_item, Item options_item, Item callback_item) {
    return js_zlib_callback_result("gunzip", js_zlib_gunzipSync, input_item, options_item, callback_item);
}

extern "C" Item js_zlib_deflate(Item input_item, Item options_item, Item callback_item) {
    return js_zlib_callback_result("deflate", js_zlib_deflateSync, input_item, options_item, callback_item);
}

extern "C" Item js_zlib_inflate(Item input_item, Item options_item, Item callback_item) {
    return js_zlib_callback_result("inflate", js_zlib_inflateSync, input_item, options_item, callback_item);
}

extern "C" Item js_zlib_deflateRaw(Item input_item, Item options_item, Item callback_item) {
    return js_zlib_callback_result("deflateRaw", js_zlib_deflateRawSync, input_item, options_item, callback_item);
}

extern "C" Item js_zlib_inflateRaw(Item input_item, Item options_item, Item callback_item) {
    return js_zlib_callback_result("inflateRaw", js_zlib_inflateRawSync, input_item, options_item, callback_item);
}

extern "C" Item js_zlib_unzip(Item input_item, Item options_item, Item callback_item) {
    return js_zlib_callback_result("unzip", js_zlib_unzipSync, input_item, options_item, callback_item);
}

// =============================================================================
// createGzip/createGunzip/etc. — Transform-backed one-shot chunk transforms
// =============================================================================

static Item zlib_constructor_prototypes[8] = {};

struct JsZlibStreamState {
    z_stream strm;
    int mode;
    int window_bits;
    bool initialized;
    bool finished;
    bool is_deflate;
};

static bool zlib_mode_is_deflate(int mode) {
    return mode == ZLIB_TRANSFORM_GZIP || mode == ZLIB_TRANSFORM_DEFLATE ||
           mode == ZLIB_TRANSFORM_DEFLATE_RAW;
}

static bool zlib_stream_should_reset_member(JsZlibStreamState* state, const uint8_t* data, int len) {
    if (!state || state->is_deflate) return false;
    if (state->mode == ZLIB_TRANSFORM_GUNZIP) return true;
    if (state->mode == ZLIB_TRANSFORM_UNZIP) return zlib_bytes_start_gzip_member(data, len);
    return false;
}

static bool zlib_option_number_value(Item value, double* out_value);

static int zlib_option_int(Item options_item, const char* name, int fallback) {
    if (get_type_id(options_item) != LMD_TYPE_MAP) return fallback;
    Item value = js_property_get(options_item, make_string_item(name));
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
            get_type_id(actual) == LMD_TYPE_FUNC ? "function" : "object");
    }
    return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
}

static Item zlib_throw_property_range_error(const char* name, const char* range, Item actual) {
    char actual_buf[64];
    zlib_format_number_for_error(actual, actual_buf, sizeof(actual_buf));
    char msg[256];
    snprintf(msg, sizeof(msg),
        "The value of \"%s\" is out of range. It must be %s. Received %s",
        name, range, actual_buf);
    return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
}

static Item zlib_throw_uint32_range_error(const char* range, Item actual) {
    char actual_buf[64];
    zlib_format_number_for_error(actual, actual_buf, sizeof(actual_buf));
    char msg[256];
    snprintf(msg, sizeof(msg),
        "The value of \"value\" is out of range. It must be %s. Received %s",
        range, actual_buf);
    return js_throw_range_error_code("ERR_OUT_OF_RANGE", msg);
}

static bool zlib_crc32_seed_value(Item value, uint32_t* out_value) {
    if (zlib_item_is_undefined(value)) {
        *out_value = 0;
        return true;
    }
    if (zlib_item_is_symbol(value)) {
        js_throw_invalid_arg_type("value", "number", value);
        return false;
    }

    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT) {
        int64_t number = it2i(value);
        if (number < 0 || number > 0xFFFFFFFFLL) {
            zlib_throw_uint32_range_error(">= 0 && <= 4294967295", value);
            return false;
        }
        *out_value = (uint32_t)number;
        return true;
    }
    if (type == LMD_TYPE_INT64) {
        int64_t number = it2l(value);
        if (number < 0 || number > 0xFFFFFFFFLL) {
            zlib_throw_uint32_range_error(">= 0 && <= 4294967295", value);
            return false;
        }
        *out_value = (uint32_t)number;
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        double number = it2d(value);
        if (number != number || number == 1.0 / 0.0 || number == -1.0 / 0.0) {
            zlib_throw_uint32_range_error("an integer", value);
            return false;
        }
        if (number < 0.0 || number > 4294967295.0) {
            zlib_throw_uint32_range_error(">= 0 && <= 4294967295", value);
            return false;
        }
        uint32_t integer = (uint32_t)number;
        if (number != (double)integer) {
            zlib_throw_uint32_range_error("an integer", value);
            return false;
        }
        *out_value = integer;
        return true;
    }

    js_throw_invalid_arg_type("value", "number", value);
    return false;
}

static bool zlib_validate_int_option(Item options_item, const char* key_name,
                                     const char* prop_name,
                                     int min_value, int max_value, bool allow_zero) {
    if (get_type_id(options_item) != LMD_TYPE_MAP) return true;
    Item value = js_property_get(options_item, make_string_item(key_name));
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

static Item zlib_state_key(void) {
    return make_string_item("__zlib_state__");
}

static Item zlib_stream_state_item(JsZlibStreamState* state) {
    if (!state) return ItemNull;
    return (Item){.item = i2it((int64_t)(uintptr_t)state)};
}

static JsZlibStreamState* zlib_stream_state_from_item(Item item) {
    if (get_type_id(item) != LMD_TYPE_INT) return NULL;
    return (JsZlibStreamState*)(uintptr_t)it2i(item);
}

static JsZlibStreamState* zlib_stream_state_from_stream(Item stream) {
    return zlib_stream_state_from_item(js_property_get(stream, zlib_state_key()));
}

static void zlib_stream_clear_state(Item stream) {
    JsZlibStreamState* state = zlib_stream_state_from_stream(stream);
    js_property_set(stream, zlib_state_key(), ItemNull);
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
    Item mode_item = js_property_get(self, make_string_item("__zlib_mode__"));
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
    Item mode_item = js_property_get(self, make_string_item("__zlib_mode__"));
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

    Item mode_item = js_property_get(self, make_string_item("__zlib_mode__"));
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

    js_property_set(stream, make_string_item("__zlib_mode__"), (Item){.item = i2it(mode)});
    js_property_set(stream, zlib_state_key(), zlib_stream_state_item(state));
    js_mark_non_enumerable(stream, zlib_state_key());
    js_property_set(stream, make_string_item("_transform"),
                    js_new_function((void*)js_zlib_transform_chunk, 3));
    js_property_set(stream, make_string_item("_flush"),
                    js_new_function((void*)js_zlib_transform_flush, 1));
    js_property_set(stream, make_string_item("_destroy"),
                    js_new_function((void*)js_zlib_transform_destroy, 2));
    js_property_set(stream, make_string_item("flush"),
                    js_new_function((void*)js_zlib_stream_flush_method, 2));

    if (mode >= ZLIB_TRANSFORM_GZIP && mode <= ZLIB_TRANSFORM_UNZIP) {
        Item proto = zlib_constructor_prototypes[mode];
        if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(stream, proto);
    }
    return stream;
}

extern "C" Item js_zlib_createGzip(Item options_item) {
    return js_zlib_create_transform(ZLIB_TRANSFORM_GZIP, options_item);
}

extern "C" Item js_zlib_createGunzip(Item options_item) {
    return js_zlib_create_transform(ZLIB_TRANSFORM_GUNZIP, options_item);
}

extern "C" Item js_zlib_createDeflate(Item options_item) {
    return js_zlib_create_transform(ZLIB_TRANSFORM_DEFLATE, options_item);
}

extern "C" Item js_zlib_createInflate(Item options_item) {
    return js_zlib_create_transform(ZLIB_TRANSFORM_INFLATE, options_item);
}

extern "C" Item js_zlib_createDeflateRaw(Item options_item) {
    return js_zlib_create_transform(ZLIB_TRANSFORM_DEFLATE_RAW, options_item);
}

extern "C" Item js_zlib_createInflateRaw(Item options_item) {
    return js_zlib_create_transform(ZLIB_TRANSFORM_INFLATE_RAW, options_item);
}

extern "C" Item js_zlib_createUnzip(Item options_item) {
    return js_zlib_create_transform(ZLIB_TRANSFORM_UNZIP, options_item);
}

// =============================================================================
// zlib Module Namespace
// =============================================================================

// crc32(data[, value]) — compute CRC32
extern "C" Item js_zlib_crc32(Item data_item, Item init_val) {
    uint32_t crc_val = 0;
    if (!zlib_crc32_seed_value(init_val, &crc_val)) return ItemNull;

    const uint8_t* data = NULL;
    int data_len = 0;
    if (!get_input_buffer(data_item, &data, &data_len)) {
        return js_throw_invalid_arg_type("data", "string, Buffer, TypedArray, or DataView", data_item);
    }
    static const uint8_t empty_crc_data = 0;
    if (data_len == 0 && !data) data = &empty_crc_data;
    crc_val = (uint32_t)crc32((uLong)crc_val, (const Bytef*)data, (uInt)data_len);

    // return as an unsigned 32-bit value represented by Lambda's signed int slot.
    return (Item){.item = i2it((int64_t)crc_val)};
}

static Item zlib_namespace = {0};

static void zlib_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    RootFrame roots((Context*)context, 3);
    Rooted<Item> ns_root(roots, ns);
    Rooted<Item> key_root(roots, make_string_item(name));
    Rooted<Item> fn_root(roots, js_new_function(func_ptr, param_count));
    js_property_set(ns_root.get(), key_root.get(), fn_root.get());
}

static Item zlib_set_constructor(Item ns, const char* name, void* func_ptr, int mode,
                                 Item transform_proto) {
    RootFrame roots((Context*)context, 4);
    Rooted<Item> ns_root(roots, ns);
    Rooted<Item> transform_proto_root(roots, transform_proto);
    Rooted<Item> ctor_root(roots, js_new_function(func_ptr, 1));
    Rooted<Item> proto_root(roots, js_new_object());
    // Constructor and prototype are mutually linked before either is
    // published in the namespace, so both need exact construction roots.
    if (get_type_id(transform_proto_root.get()) == LMD_TYPE_MAP) {
        js_set_prototype(proto_root.get(), transform_proto_root.get());
    }
    js_property_set(proto_root.get(), make_string_item("constructor"), ctor_root.get());
    js_mark_non_enumerable(proto_root.get(), make_string_item("constructor"));
    js_property_set(ctor_root.get(), make_string_item("prototype"), proto_root.get());
    js_function_set_prototype(ctor_root.get(), proto_root.get());
    js_set_function_name(ctor_root.get(), make_string_item(name));
    if (mode >= ZLIB_TRANSFORM_GZIP && mode <= ZLIB_TRANSFORM_UNZIP) {
        zlib_constructor_prototypes[mode] = proto_root.get();
    }
    js_property_set(ns_root.get(), make_string_item(name), ctor_root.get());
    return ctor_root.get();
}

extern "C" Item js_get_zlib_namespace(void) {
    heap_register_gc_root(&zlib_namespace.item);
    if (zlib_namespace.item != 0) return zlib_namespace;

    zlib_namespace = js_new_object();
    RootFrame roots((Context*)context, 6);
    Rooted<Item> ns_root(roots, zlib_namespace);
    Rooted<Item> stream_root(roots, ItemNull);
    Rooted<Item> transform_ctor_root(roots, ItemNull);
    Rooted<Item> transform_proto_root(roots, ItemNull);
    Rooted<Item> constants_root(roots, ItemNull);
    Rooted<Item> codes_root(roots, ItemNull);
    // The namespace is persistent, while stream-derived prototypes and the
    // two frozen tables remain unpublished during allocating initialization.

    stream_root.set(js_get_stream_namespace());
    transform_ctor_root.set(js_property_get(stream_root.get(), make_string_item("Transform")));
    transform_proto_root.set(js_property_get(transform_ctor_root.get(), make_string_item("prototype")));

    zlib_set_constructor(ns_root.get(), "Gzip",       (void*)js_zlib_createGzip,
                         ZLIB_TRANSFORM_GZIP, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "Gunzip",     (void*)js_zlib_createGunzip,
                         ZLIB_TRANSFORM_GUNZIP, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "Deflate",    (void*)js_zlib_createDeflate,
                         ZLIB_TRANSFORM_DEFLATE, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "Inflate",    (void*)js_zlib_createInflate,
                         ZLIB_TRANSFORM_INFLATE, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "DeflateRaw", (void*)js_zlib_createDeflateRaw,
                         ZLIB_TRANSFORM_DEFLATE_RAW, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "InflateRaw", (void*)js_zlib_createInflateRaw,
                         ZLIB_TRANSFORM_INFLATE_RAW, transform_proto_root.get());
    zlib_set_constructor(ns_root.get(), "Unzip",      (void*)js_zlib_createUnzip,
                         ZLIB_TRANSFORM_UNZIP, transform_proto_root.get());

    zlib_set_method(zlib_namespace, "gzip",                (void*)js_zlib_gzip, 3);
    zlib_set_method(zlib_namespace, "gunzip",              (void*)js_zlib_gunzip, 3);
    zlib_set_method(zlib_namespace, "deflate",             (void*)js_zlib_deflate, 3);
    zlib_set_method(zlib_namespace, "inflate",             (void*)js_zlib_inflate, 3);
    zlib_set_method(zlib_namespace, "deflateRaw",          (void*)js_zlib_deflateRaw, 3);
    zlib_set_method(zlib_namespace, "inflateRaw",          (void*)js_zlib_inflateRaw, 3);
    zlib_set_method(zlib_namespace, "unzip",               (void*)js_zlib_unzip, 3);
    zlib_set_method(zlib_namespace, "gzipSync",            (void*)js_zlib_gzipSync, 1);
    zlib_set_method(zlib_namespace, "gunzipSync",          (void*)js_zlib_gunzipSync, 1);
    zlib_set_method(zlib_namespace, "deflateSync",         (void*)js_zlib_deflateSync, 1);
    zlib_set_method(zlib_namespace, "inflateSync",         (void*)js_zlib_inflateSync, 1);
    zlib_set_method(zlib_namespace, "deflateRawSync",      (void*)js_zlib_deflateRawSync, 1);
    zlib_set_method(zlib_namespace, "inflateRawSync",      (void*)js_zlib_inflateRawSync, 1);
    zlib_set_method(zlib_namespace, "brotliCompressSync",  (void*)js_zlib_brotliCompressSync, 1);
    zlib_set_method(zlib_namespace, "brotliDecompressSync",(void*)js_zlib_brotliDecompressSync, 1);
    zlib_set_method(zlib_namespace, "unzipSync",           (void*)js_zlib_unzipSync, 1);
    zlib_set_method(zlib_namespace, "crc32",               (void*)js_zlib_crc32, 2);
    zlib_set_method(zlib_namespace, "createGzip",          (void*)js_zlib_createGzip, 1);
    zlib_set_method(zlib_namespace, "createGunzip",        (void*)js_zlib_createGunzip, 1);
    zlib_set_method(zlib_namespace, "createDeflate",       (void*)js_zlib_createDeflate, 1);
    zlib_set_method(zlib_namespace, "createInflate",       (void*)js_zlib_createInflate, 1);
    zlib_set_method(zlib_namespace, "createDeflateRaw",    (void*)js_zlib_createDeflateRaw, 1);
    zlib_set_method(zlib_namespace, "createInflateRaw",    (void*)js_zlib_createInflateRaw, 1);
    zlib_set_method(zlib_namespace, "createUnzip",         (void*)js_zlib_createUnzip, 1);

    // constants — all zlib constants including flush modes, error codes, compression levels, strategies
    extern Item js_object_freeze(Item obj);
    Item constants = js_new_object();
    constants_root.set(constants);
    // flush modes
    js_property_set(constants, make_string_item("Z_NO_FLUSH"),      (Item){.item = i2it(Z_NO_FLUSH)});
    js_property_set(constants, make_string_item("Z_PARTIAL_FLUSH"), (Item){.item = i2it(Z_PARTIAL_FLUSH)});
    js_property_set(constants, make_string_item("Z_SYNC_FLUSH"),    (Item){.item = i2it(Z_SYNC_FLUSH)});
    js_property_set(constants, make_string_item("Z_FULL_FLUSH"),    (Item){.item = i2it(Z_FULL_FLUSH)});
    js_property_set(constants, make_string_item("Z_FINISH"),        (Item){.item = i2it(Z_FINISH)});
    js_property_set(constants, make_string_item("Z_BLOCK"),         (Item){.item = i2it(Z_BLOCK)});
    js_property_set(constants, make_string_item("Z_TREES"),         (Item){.item = i2it(Z_TREES)});
    // error codes
    js_property_set(constants, make_string_item("Z_OK"),            (Item){.item = i2it(Z_OK)});
    js_property_set(constants, make_string_item("Z_STREAM_END"),    (Item){.item = i2it(Z_STREAM_END)});
    js_property_set(constants, make_string_item("Z_NEED_DICT"),     (Item){.item = i2it(Z_NEED_DICT)});
    js_property_set(constants, make_string_item("Z_ERRNO"),         (Item){.item = i2it(Z_ERRNO)});
    js_property_set(constants, make_string_item("Z_STREAM_ERROR"),  (Item){.item = i2it(Z_STREAM_ERROR)});
    js_property_set(constants, make_string_item("Z_DATA_ERROR"),    (Item){.item = i2it(Z_DATA_ERROR)});
    js_property_set(constants, make_string_item("Z_MEM_ERROR"),     (Item){.item = i2it(Z_MEM_ERROR)});
    js_property_set(constants, make_string_item("Z_BUF_ERROR"),     (Item){.item = i2it(Z_BUF_ERROR)});
    js_property_set(constants, make_string_item("Z_VERSION_ERROR"), (Item){.item = i2it(Z_VERSION_ERROR)});
    // compression levels
    js_property_set(constants, make_string_item("Z_NO_COMPRESSION"),      (Item){.item = i2it(Z_NO_COMPRESSION)});
    js_property_set(constants, make_string_item("Z_BEST_SPEED"),          (Item){.item = i2it(Z_BEST_SPEED)});
    js_property_set(constants, make_string_item("Z_BEST_COMPRESSION"),    (Item){.item = i2it(Z_BEST_COMPRESSION)});
    js_property_set(constants, make_string_item("Z_DEFAULT_COMPRESSION"), (Item){.item = i2it(Z_DEFAULT_COMPRESSION)});
    // strategies
    js_property_set(constants, make_string_item("Z_FILTERED"),         (Item){.item = i2it(Z_FILTERED)});
    js_property_set(constants, make_string_item("Z_HUFFMAN_ONLY"),     (Item){.item = i2it(Z_HUFFMAN_ONLY)});
    js_property_set(constants, make_string_item("Z_RLE"),              (Item){.item = i2it(Z_RLE)});
    js_property_set(constants, make_string_item("Z_FIXED"),            (Item){.item = i2it(Z_FIXED)});
    js_property_set(constants, make_string_item("Z_DEFAULT_STRATEGY"), (Item){.item = i2it(Z_DEFAULT_STRATEGY)});
    // window bits / mem level
    js_property_set(constants, make_string_item("Z_MIN_WINDOWBITS"),  (Item){.item = i2it(8)});
    js_property_set(constants, make_string_item("Z_MAX_WINDOWBITS"),  (Item){.item = i2it(15)});
    js_property_set(constants, make_string_item("Z_DEFAULT_WINDOWBITS"), (Item){.item = i2it(15)});
    js_property_set(constants, make_string_item("Z_MIN_CHUNK"),       (Item){.item = i2it(64)});
    js_property_set(constants, make_string_item("Z_MAX_CHUNK"),       (Item){.item = i2it(INT_MAX)}); // INT_CAST_OK: zlib constant
    js_property_set(constants, make_string_item("Z_DEFAULT_CHUNK"),   (Item){.item = i2it(16384)});
    js_property_set(constants, make_string_item("Z_MIN_MEMLEVEL"),    (Item){.item = i2it(1)});
    js_property_set(constants, make_string_item("Z_MAX_MEMLEVEL"),    (Item){.item = i2it(9)});
    js_property_set(constants, make_string_item("Z_DEFAULT_MEMLEVEL"),(Item){.item = i2it(8)});
    js_property_set(constants, make_string_item("Z_MIN_LEVEL"),       (Item){.item = i2it(-1)});
    js_property_set(constants, make_string_item("Z_MAX_LEVEL"),       (Item){.item = i2it(9)});
    js_property_set(constants, make_string_item("DEFLATE"),           (Item){.item = i2it(1)});
    js_property_set(constants, make_string_item("INFLATE"),           (Item){.item = i2it(2)});
    js_property_set(constants, make_string_item("GZIP"),              (Item){.item = i2it(3)});
    js_property_set(constants, make_string_item("GUNZIP"),            (Item){.item = i2it(4)});
    js_property_set(constants, make_string_item("DEFLATERAW"),        (Item){.item = i2it(5)});
    js_property_set(constants, make_string_item("INFLATERAW"),        (Item){.item = i2it(6)});
    js_property_set(constants, make_string_item("UNZIP"),             (Item){.item = i2it(7)});
    js_object_freeze(constants);
    Item constants_key = make_string_item("constants");
    js_property_set(zlib_namespace, constants_key, constants);
    js_mark_non_writable(zlib_namespace, constants_key);
    js_mark_non_configurable(zlib_namespace, constants_key);

    // codes — error code map (frozen)
    Item codes = js_new_object();
    codes_root.set(codes);
    js_property_set(codes, make_string_item("Z_OK"),              (Item){.item = i2it(Z_OK)});
    js_property_set(codes, make_string_item("Z_STREAM_END"),      (Item){.item = i2it(Z_STREAM_END)});
    js_property_set(codes, make_string_item("Z_NEED_DICT"),       (Item){.item = i2it(Z_NEED_DICT)});
    js_property_set(codes, make_string_item("Z_ERRNO"),           (Item){.item = i2it(Z_ERRNO)});
    js_property_set(codes, make_string_item("Z_STREAM_ERROR"),    (Item){.item = i2it(Z_STREAM_ERROR)});
    js_property_set(codes, make_string_item("Z_DATA_ERROR"),      (Item){.item = i2it(Z_DATA_ERROR)});
    js_property_set(codes, make_string_item("Z_MEM_ERROR"),       (Item){.item = i2it(Z_MEM_ERROR)});
    js_property_set(codes, make_string_item("Z_BUF_ERROR"),       (Item){.item = i2it(Z_BUF_ERROR)});
    js_property_set(codes, make_string_item("Z_VERSION_ERROR"),   (Item){.item = i2it(Z_VERSION_ERROR)});
    js_object_freeze(codes);
    Item codes_key = make_string_item("codes");
    js_property_set(zlib_namespace, codes_key, codes);
    js_mark_non_writable(zlib_namespace, codes_key);
    js_mark_non_configurable(zlib_namespace, codes_key);

    Item default_key = make_string_item("default");
    js_property_set(zlib_namespace, default_key, zlib_namespace);

    return zlib_namespace;
}

extern "C" void js_zlib_reset(void) {
    zlib_namespace = (Item){0};
    for (int i = 0; i < 8; i++) zlib_constructor_prototypes[i] = (Item){0};
}
