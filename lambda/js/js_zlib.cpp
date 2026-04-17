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
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/mem.h"

#include <cstring>
#include <zlib.h>

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, strlen(str));
    return (Item){.item = s2it(s)};
}

// extract buffer data from Uint8Array or string
static bool get_input_buffer(Item input, const uint8_t** out, int* out_len) {
    if (js_is_typed_array(input)) {
        JsTypedArray* ta = (JsTypedArray*)input.map->data;
        if (ta && ta->data) {
            *out = (const uint8_t*)ta->data;
            *out_len = ta->length;
            return true;
        }
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
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (ta && ta->data) memcpy(ta->data, data, (size_t)len);
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
    deflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        log_error("zlib: gzipSync: deflate failed with %d", ret);
        mem_free(out_buf);
        return ItemNull;
    }

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

    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        log_error("zlib: gunzipSync: inflate failed with %d", ret);
        mem_free(out_buf);
        return ItemNull;
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
    deflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        mem_free(out_buf);
        return ItemNull;
    }

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

    inflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        mem_free(out_buf);
        return ItemNull;
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
    deflateEnd(&strm);

    if (ret != Z_STREAM_END) { mem_free(out_buf); return ItemNull; }

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

    inflateEnd(&strm);

    if (ret != Z_STREAM_END) { mem_free(out_buf); return ItemNull; }

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
// zlib Module Namespace
// =============================================================================

// crc32(data[, value]) — compute CRC32
extern "C" Item js_zlib_crc32(Item data_item, Item init_val) {
    unsigned long crc_val = 0;
    if (get_type_id(init_val) == LMD_TYPE_INT) {
        crc_val = (unsigned long)it2i(init_val);
    }

    TypeId tid = get_type_id(data_item);
    if (tid == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        if (s && s->len > 0) {
            crc_val = crc32(crc_val, (const Bytef*)s->chars, (uInt)s->len);
        }
    } else if (tid == LMD_TYPE_MAP) {
        // TypedArray / Buffer — get data pointer via internal struct
        Map* m = data_item.map;
        if (m && m->map_kind == MAP_KIND_TYPED_ARRAY && m->data) {
            struct TaHeader { int element_type; int length; int byte_length; int byte_offset; void* data; };
            TaHeader* ta = (TaHeader*)m->data;
            if (ta->data && ta->byte_length > 0) {
                crc_val = crc32(crc_val, (const Bytef*)ta->data, (uInt)ta->byte_length);
            }
        }
    }
    // return as unsigned 32-bit integer
    return (Item){.item = i2it((int32_t)(crc_val & 0xFFFFFFFF))};
}

static Item zlib_namespace = {0};

static void zlib_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_zlib_namespace(void) {
    if (zlib_namespace.item != 0) return zlib_namespace;

    zlib_namespace = js_new_object();

    zlib_set_method(zlib_namespace, "gzipSync",            (void*)js_zlib_gzipSync, 1);
    zlib_set_method(zlib_namespace, "gunzipSync",          (void*)js_zlib_gunzipSync, 1);
    zlib_set_method(zlib_namespace, "deflateSync",         (void*)js_zlib_deflateSync, 1);
    zlib_set_method(zlib_namespace, "inflateSync",         (void*)js_zlib_inflateSync, 1);
    zlib_set_method(zlib_namespace, "deflateRawSync",      (void*)js_zlib_deflateRawSync, 1);
    zlib_set_method(zlib_namespace, "inflateRawSync",      (void*)js_zlib_inflateRawSync, 1);
    zlib_set_method(zlib_namespace, "brotliCompressSync",  (void*)js_zlib_brotliCompressSync, 1);
    zlib_set_method(zlib_namespace, "brotliDecompressSync",(void*)js_zlib_brotliDecompressSync, 1);
    // unzipSync is alias for gunzipSync (handles both gzip and deflate)
    zlib_set_method(zlib_namespace, "unzipSync",           (void*)js_zlib_gunzipSync, 1);
    zlib_set_method(zlib_namespace, "crc32",               (void*)js_zlib_crc32, 2);

    // constants
    Item constants = js_new_object();
    js_property_set(constants, make_string_item("Z_NO_FLUSH"),      (Item){.item = i2it(Z_NO_FLUSH)});
    js_property_set(constants, make_string_item("Z_SYNC_FLUSH"),    (Item){.item = i2it(Z_SYNC_FLUSH)});
    js_property_set(constants, make_string_item("Z_FINISH"),        (Item){.item = i2it(Z_FINISH)});
    js_property_set(constants, make_string_item("Z_DEFAULT_COMPRESSION"), (Item){.item = i2it(Z_DEFAULT_COMPRESSION)});
    js_property_set(constants, make_string_item("Z_BEST_SPEED"),    (Item){.item = i2it(Z_BEST_SPEED)});
    js_property_set(constants, make_string_item("Z_BEST_COMPRESSION"), (Item){.item = i2it(Z_BEST_COMPRESSION)});
    js_property_set(zlib_namespace, make_string_item("constants"), constants);

    // codes — error code map
    Item codes = js_new_object();
    js_property_set(codes, make_string_item("Z_OK"),              (Item){.item = i2it(Z_OK)});
    js_property_set(codes, make_string_item("Z_STREAM_END"),      (Item){.item = i2it(Z_STREAM_END)});
    js_property_set(codes, make_string_item("Z_NEED_DICT"),       (Item){.item = i2it(Z_NEED_DICT)});
    js_property_set(codes, make_string_item("Z_ERRNO"),           (Item){.item = i2it(Z_ERRNO)});
    js_property_set(codes, make_string_item("Z_STREAM_ERROR"),    (Item){.item = i2it(Z_STREAM_ERROR)});
    js_property_set(codes, make_string_item("Z_DATA_ERROR"),      (Item){.item = i2it(Z_DATA_ERROR)});
    js_property_set(codes, make_string_item("Z_MEM_ERROR"),       (Item){.item = i2it(Z_MEM_ERROR)});
    js_property_set(codes, make_string_item("Z_BUF_ERROR"),       (Item){.item = i2it(Z_BUF_ERROR)});
    js_property_set(codes, make_string_item("Z_VERSION_ERROR"),   (Item){.item = i2it(Z_VERSION_ERROR)});
    js_property_set(zlib_namespace, make_string_item("codes"), codes);

    Item default_key = make_string_item("default");
    js_property_set(zlib_namespace, default_key, zlib_namespace);

    return zlib_namespace;
}

extern "C" void js_zlib_reset(void) {
    zlib_namespace = (Item){0};
}
