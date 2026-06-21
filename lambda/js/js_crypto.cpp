/**
 * Native SHA-256/384/512, HMAC, randomBytes, randomUUID, createHash for Lambda JS engine.
 * 
 * These replace the JavaScript Word64-based SHA implementations which are
 * extremely slow due to per-operation method call overhead through JS dispatch.
 * The native versions compute SHA directly on raw buffers and return Uint8Array.
 *
 * Phase 4 additions: HMAC (native), randomBytes (OS entropy), randomUUID (v4),
 * createHash (streaming hash API), crypto module namespace.
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "js_error_codes.h"
#include "../lambda-data.hpp"

extern "C" Item js_get_current_this(void);
extern "C" Item js_process_emit(Item event_name, Item arg1);
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include <cstring>
#include "../../lib/mem.h"
#include "../../lib/hex.h"
#include "../../lib/base64.h"
#include "../../lib/uuid.h"
#include "../../lib/digest.h"
#include <mbedtls/bignum.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#endif

// ============================================================================
// Digest helpers
// ============================================================================

static const uint8_t crypto_empty_bytes[1] = {0};
static Item crypto_namespace = {0};

static int crypto_digest_bits_for_name_ext(const char* digest, bool allow_md5, bool allow_sha1, bool allow_sha224) {
    if (!digest) return 0;
    if (allow_md5 && strcmp(digest, "md5") == 0) return DIGEST_MD5;
    if (allow_sha1 && strcmp(digest, "sha1") == 0) return DIGEST_SHA1;
    if (strcmp(digest, "sha256") == 0) return DIGEST_SHA256;
    if (strcmp(digest, "sha384") == 0) return DIGEST_SHA384;
    if (strcmp(digest, "sha512") == 0) return DIGEST_SHA512;
    if (allow_sha224 && strcmp(digest, "sha224") == 0) return DIGEST_SHA224;
    return 0;
}

static int crypto_digest_bits_for_name(const char* digest, bool allow_sha1, bool allow_sha224) {
    return crypto_digest_bits_for_name_ext(digest, false, allow_sha1, allow_sha224);
}

static bool crypto_normalize_string(Item item, char* out, int out_size, bool strip_dash);

static bool crypto_normalize_sign_verify_digest(Item alg_item, char* out, int out_size) {
    if (!out || out_size <= 0) return false;
    if (!crypto_normalize_string(alg_item, out, out_size, true)) return false;
    if (crypto_digest_bits_for_name_ext(out, true, true, true) != 0) return true;

    const char* digest = NULL;
    if (strncmp(out, "rsa", 3) == 0) {
        digest = out + 3;
    } else if (strncmp(out, "dsa", 3) == 0) {
        digest = out + 3;
    } else if (strncmp(out, "ecdsa", 5) == 0) {
        digest = out + 5;
    } else if (strcmp(out, "dss1") == 0) {
        digest = "sha1";
    }

    if (digest && crypto_digest_bits_for_name_ext(digest, true, true, true) != 0) {
        int len = (int)strlen(digest);
        if (len >= out_size) len = out_size - 1;
        memmove(out, digest, (size_t)len);
        out[len] = '\0';
        return true;
    }
    return false;
}

static bool crypto_digest_compute_bits(int bits, const uint8_t* data,
                                  int offset, int length, uint8_t* out) {
    if (offset < 0) offset = 0;
    if (length < 0) length = 0;
    const uint8_t* input = data ? data + offset : crypto_empty_bytes;
    size_t digest_len = digest_output_len_bits(bits);
    if (digest_len == 0 || !out) return false;
    if (!digest_compute_bits(bits, input, (size_t)length, out, digest_len)) {
        log_error("crypto: digest compute failed for bits=%d", bits);
        return false;
    }
    return true;
}

static void sha256_compute(const uint8_t* data, int offset, int length, uint8_t* out) {
    if (!crypto_digest_compute_bits(DIGEST_SHA256, data, offset, length, out) && out) {
        memset(out, 0, 32);
    }
}

static void sha512_compute(const uint8_t* data, int offset, int length, bool mode384, uint8_t* out) {
    int bits = mode384 ? DIGEST_SHA384 : DIGEST_SHA512;
    if (!crypto_digest_compute_bits(bits, data, offset, length, out) && out) {
        memset(out, 0, mode384 ? 48 : 64);
    }
}

// ============================================================================
// Helper: extract uint8 buffer from a JS typed array Item
// ============================================================================

static bool get_uint8_buffer(Item ta_item, const uint8_t** out_data, int* out_length) {
    if (!js_is_typed_array(ta_item)) return false;
    int byte_len = js_typed_array_byte_length(ta_item);
    void* data = js_typed_array_current_data_ptr(ta_item);
    if (!data && byte_len > 0) return false;
    *out_data = data ? (const uint8_t*)data : crypto_empty_bytes;
    *out_length = byte_len;
    return true;
}

// ============================================================================
// JS-callable functions: calculateSHA256, calculateSHA384, calculateSHA512
// All take (data: Uint8Array|Array, offset: int, length: int)
// and return a new Uint8Array with the hash result.
// ============================================================================

extern "C" Item js_native_sha256(Item data_item, Item offset_item, Item length_item) {
    int offset = (int)it2i(offset_item);
    int length = (int)it2i(length_item);

    const uint8_t* buf = NULL;
    int buf_len = 0;
    if (!get_uint8_buffer(data_item, &buf, &buf_len)) {
        log_error("js_native_sha256: data is not a typed array");
        return (Item){.item = ITEM_NULL};
    }
    if (offset < 0) offset = 0;
    if (offset + length > buf_len) length = buf_len - offset;
    if (length < 0) length = 0;

    uint8_t hash[32];
    sha256_compute(buf, offset, length, hash);

    Item result = js_typed_array_new(JS_TYPED_UINT8, 32);
    JsTypedArray* rta = (JsTypedArray*)result.map->data;
    if (rta && rta->data) {
        memcpy(rta->data, hash, 32);
    }
    return result;
}

extern "C" Item js_native_sha384(Item data_item, Item offset_item, Item length_item) {
    int offset = (int)it2i(offset_item);
    int length = (int)it2i(length_item);

    const uint8_t* buf = NULL;
    int buf_len = 0;
    if (!get_uint8_buffer(data_item, &buf, &buf_len)) {
        log_error("js_native_sha384: data is not a typed array");
        return (Item){.item = ITEM_NULL};
    }
    if (offset < 0) offset = 0;
    if (offset + length > buf_len) length = buf_len - offset;
    if (length < 0) length = 0;

    uint8_t hash[48];
    sha512_compute(buf, offset, length, true, hash);

    Item result = js_typed_array_new(JS_TYPED_UINT8, 48);
    JsTypedArray* rta = (JsTypedArray*)result.map->data;
    if (rta && rta->data) {
        memcpy(rta->data, hash, 48);
    }
    return result;
}

extern "C" Item js_native_sha512(Item data_item, Item offset_item, Item length_item) {
    int offset = (int)it2i(offset_item);
    int length = (int)it2i(length_item);

    const uint8_t* buf = NULL;
    int buf_len = 0;
    if (!get_uint8_buffer(data_item, &buf, &buf_len)) {
        log_error("js_native_sha512: data is not a typed array");
        return (Item){.item = ITEM_NULL};
    }
    if (offset < 0) offset = 0;
    if (offset + length > buf_len) length = buf_len - offset;
    if (length < 0) length = 0;

    uint8_t hash[64];
    sha512_compute(buf, offset, length, false, hash);

    Item result = js_typed_array_new(JS_TYPED_UINT8, 64);
    JsTypedArray* rta = (JsTypedArray*)result.map->data;
    if (rta && rta->data) {
        memcpy(rta->data, hash, 64);
    }
    return result;
}

// ============================================================================
// OS-level random bytes
// ============================================================================

static bool crypto_random_bytes(uint8_t* buf, size_t len) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t total = 0;
    while (total < len) {
        ssize_t r = read(fd, buf + total, len - total);
        if (r <= 0) { close(fd); return false; }
        total += (size_t)r;
    }
    close(fd);
    return true;
#endif
}

// ============================================================================
// randomBytes(size) → Uint8Array
// ============================================================================

static Item make_string_item_crypto(const char* str) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, strlen(str));
    return (Item){.item = s2it(s)};
}

static inline Item make_js_undefined_crypto() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

static bool crypto_item_is_undefined(Item item) {
    return item.item == ITEM_JS_UNDEFINED || get_type_id(item) == LMD_TYPE_UNDEFINED;
}

static bool extract_bytes(Item item, uint8_t** out, int* out_len);

static Item crypto_buffer_from_bytes(const uint8_t* bytes, int len) {
    if (len < 0) len = 0;
    Item result = js_typed_array_new(JS_TYPED_UINT8, len);
    JsTypedArray* ta = js_get_typed_array_ptr(result.map);
    if (ta) {
        ta->is_buffer = true;
        if (ta->data && bytes && len > 0) memcpy(ta->data, bytes, (size_t)len);
    }
    return result;
}

static void crypto_format_number_for_error(Item item, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT) {
        snprintf(out, out_size, "%lld", (long long)it2i(item));
        return;
    }
    if (type == LMD_TYPE_INT64) {
        snprintf(out, out_size, "%lld", (long long)it2l(item));
        return;
    }
    if (type == LMD_TYPE_FLOAT) {
        double value = it2d(item);
        if (value != value) {
            snprintf(out, out_size, "NaN");
        } else if (value == 1.0 / 0.0) {
            snprintf(out, out_size, "Infinity");
        } else if (value == -1.0 / 0.0) {
            snprintf(out, out_size, "-Infinity");
        } else if (value == (double)(int64_t)value) {
            snprintf(out, out_size, "%lld", (long long)(int64_t)value);
        } else {
            snprintf(out, out_size, "%g", value);
        }
        return;
    }
    snprintf(out, out_size, "undefined");
}

static void crypto_format_invalid_received(Item actual, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    TypeId type = get_type_id(actual);
    if (type == LMD_TYPE_NULL) {
        snprintf(out, out_size, " Received null");
        return;
    }
    if (type == LMD_TYPE_UNDEFINED) {
        snprintf(out, out_size, " Received undefined");
        return;
    }
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(actual);
        int len = s ? (int)s->len : 0;
        if (len > 25) len = 25;
        snprintf(out, out_size, " Received type string ('%.*s%s')",
            len, s ? s->chars : "", (s && s->len > 25) ? "..." : "");
        return;
    }
    if (type == LMD_TYPE_BOOL) {
        snprintf(out, out_size, " Received type boolean (%s)",
            actual.item == ITEM_TRUE ? "true" : "false");
        return;
    }
    if (type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT || type == LMD_TYPE_INT64) {
        char num_buf[64];
        crypto_format_number_for_error(actual, num_buf, sizeof(num_buf));
        snprintf(out, out_size, " Received type number (%s)", num_buf);
        return;
    }
    if (type == LMD_TYPE_ARRAY) {
        snprintf(out, out_size, " Received an instance of Array");
        return;
    }
    if (type == LMD_TYPE_FUNC) {
        snprintf(out, out_size, " Received function ");
        return;
    }
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_OBJECT || type == LMD_TYPE_ELEMENT || type == LMD_TYPE_VMAP) {
        snprintf(out, out_size, " Received an instance of Object");
        return;
    }
    snprintf(out, out_size, " Received type object");
}

static Item crypto_throw_size_out_of_range(Item actual) {
    char actual_buf[64];
    crypto_format_number_for_error(actual, actual_buf, sizeof(actual_buf));
    char msg[192];
    snprintf(msg, sizeof(msg),
        "The value of \"size\" is out of range. It must be >= 0 && <= 2147483647. Received %s",
        actual_buf);
    return js_throw_range_error_code(JS_ERR_OUT_OF_RANGE, msg);
}

static bool crypto_size_to_int(Item size_item, int* out_size) {
    if (!out_size) return false;
    TypeId type = get_type_id(size_item);
    double value = 0.0;
    if (type == LMD_TYPE_INT) {
        value = (double)it2i(size_item);
    } else if (type == LMD_TYPE_FLOAT) {
        value = it2d(size_item);
    } else if (type == LMD_TYPE_INT64) {
        value = (double)it2l(size_item);
    } else {
        js_throw_invalid_arg_type("size", "number", size_item);
        return false;
    }

    if (value != value || value < 0.0 || value > 2147483647.0) {
        crypto_throw_size_out_of_range(size_item);
        return false;
    }

    *out_size = (int)value;
    return true;
}

extern "C" Item js_crypto_randomBytes(Item size_item, Item callback_item) {
    int size = 0;
    if (!crypto_size_to_int(size_item, &size)) return ItemNull;

    bool has_callback = !crypto_item_is_undefined(callback_item);
    if (has_callback && get_type_id(callback_item) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "Function", callback_item);
    }

    Item result = crypto_buffer_from_bytes(NULL, size);
    JsTypedArray* ta = js_get_typed_array_ptr(result.map);
    if (!ta || (!ta->data && size > 0)) return ItemNull;
    if (size > 0 && !crypto_random_bytes((uint8_t*)ta->data, (size_t)size)) {
        log_error("crypto: randomBytes: entropy source failed");
        return ItemNull;
    }

    if (has_callback) {
        Item args[2] = { ItemNull, result };
        js_call_function(callback_item, ItemNull, args, 2);
        return make_js_undefined_crypto();
    }
    return result;
}

static bool crypto_pseudo_random_warning_emitted = false;

extern "C" Item js_crypto_pseudoRandomBytes(Item size_item, Item callback_item) {
    if (!crypto_pseudo_random_warning_emitted) {
        crypto_pseudo_random_warning_emitted = true;
        Item warning = js_new_object();
        js_property_set(warning, make_string_item_crypto("name"),
            make_string_item_crypto("DeprecationWarning"));
        js_property_set(warning, make_string_item_crypto("message"),
            make_string_item_crypto("crypto.pseudoRandomBytes is deprecated."));
        js_property_set(warning, make_string_item_crypto("code"),
            make_string_item_crypto("DEP0115"));
        js_process_emit(make_string_item_crypto("warning"), warning);
    }
    return js_crypto_randomBytes(size_item, callback_item);
}

static Item crypto_throw_fill_out_of_range(const char* name, int max_value, Item actual) {
    char actual_buf[64];
    crypto_format_number_for_error(actual, actual_buf, sizeof(actual_buf));
    char msg[192];
    snprintf(msg, sizeof(msg),
        "The value of \"%s\" is out of range. It must be >= 0 && <= %d. Received %s",
        name, max_value, actual_buf);
    return js_throw_range_error_code(JS_ERR_OUT_OF_RANGE, msg);
}

static Item crypto_throw_size_offset_out_of_range(int max_value, int64_t received) {
    char msg[160];
    snprintf(msg, sizeof(msg),
        "The value of \"size + offset\" is out of range. It must be <= %d. Received %lld",
        max_value, (long long)received);
    return js_throw_range_error_code(JS_ERR_OUT_OF_RANGE, msg);
}

static bool crypto_to_int_index(Item item, int default_value, int max_value, int* out_value, const char* name) {
    if (!out_value) return false;
    if (crypto_item_is_undefined(item)) {
        *out_value = default_value;
        return true;
    }

    double value = 0.0;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT) {
        value = (double)it2i(item);
    } else if (type == LMD_TYPE_FLOAT) {
        value = it2d(item);
    } else if (type == LMD_TYPE_INT64) {
        value = (double)it2l(item);
    } else {
        js_throw_invalid_arg_type(name, "number", item);
        return false;
    }

    if (value != value || value < 0.0 || value > (double)max_value) {
        crypto_throw_fill_out_of_range(name, max_value, item);
        return false;
    }

    *out_value = (int)value;
    return true;
}

static bool crypto_get_fill_target(Item target_item, uint8_t** out_data, int* out_len) {
    if (!out_data || !out_len) return false;
    *out_data = NULL;
    *out_len = 0;

    if (js_is_typed_array(target_item)) {
        int byte_len = js_typed_array_byte_length(target_item);
        void* data = js_typed_array_current_data_ptr(target_item);
        if (!data && byte_len > 0) {
            js_throw_type_error("Cannot perform randomFillSync on an out-of-bounds TypedArray");
            return false;
        }
        *out_data = (uint8_t*)data;
        *out_len = byte_len;
        return true;
    }

    if (js_is_dataview(target_item)) {
        JsDataView* dv = js_get_dataview_ptr(target_item);
        if (!dv || !dv->buffer || dv->buffer->detached) {
            js_throw_type_error("Cannot perform randomFillSync on a detached DataView");
            return false;
        }
        int byte_len = dv->length_tracking ? (dv->buffer->byte_length - dv->byte_offset) : dv->byte_length;
        if (byte_len < 0) byte_len = 0;
        *out_data = dv->buffer->data ? ((uint8_t*)dv->buffer->data + dv->byte_offset) : NULL;
        *out_len = byte_len;
        return true;
    }

    if (js_is_arraybuffer(target_item)) {
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(target_item);
        if (!ab || ab->detached) {
            js_throw_type_error("Cannot perform randomFillSync on a detached ArrayBuffer");
            return false;
        }
        *out_data = (uint8_t*)ab->data;
        *out_len = ab->byte_length;
        return true;
    }

    js_throw_invalid_arg_type("buf", "ArrayBuffer, Buffer, TypedArray, or DataView", target_item);
    return false;
}

// randomFillSync(buf[, offset[, size]]) → buf
extern "C" Item js_crypto_randomFillSync(Item target_item, Item offset_item, Item size_item) {
    uint8_t* data = NULL;
    int byte_len = 0;
    if (!crypto_get_fill_target(target_item, &data, &byte_len)) return ItemNull;

    int offset = 0;
    if (!crypto_to_int_index(offset_item, 0, byte_len, &offset, "offset")) return ItemNull;

    int size = byte_len - offset;
    if (!crypto_to_int_index(size_item, size, 2147483647, &size, "size")) return ItemNull;
    int64_t end = (int64_t)offset + (int64_t)size;
    if (end > byte_len) {
        return crypto_throw_size_offset_out_of_range(byte_len, end);
    }

    if (size > 0 && data) {
        if (!crypto_random_bytes(data + offset, (size_t)size)) {
            log_error("crypto: randomFillSync: entropy source failed");
            return ItemNull;
        }
    }
    return target_item;
}

// randomFill(buf[, offset[, size]], callback) → undefined
extern "C" Item js_crypto_randomFill(Item target_item, Item offset_item, Item size_item, Item callback_item) {
    Item callback = callback_item;
    Item offset = offset_item;
    Item size = size_item;

    if (get_type_id(offset_item) == LMD_TYPE_FUNC) {
        callback = offset_item;
        offset = make_js_undefined_crypto();
        size = make_js_undefined_crypto();
    } else if (get_type_id(size_item) == LMD_TYPE_FUNC) {
        callback = size_item;
        size = make_js_undefined_crypto();
    }

    if (get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "Function", callback);
    }

    Item filled = js_crypto_randomFillSync(target_item, offset, size);
    if (filled.item == ITEM_NULL) return ItemNull;

    Item args[2] = { ItemNull, filled };
    js_call_function(callback, ItemNull, args, 2);
    return make_js_undefined_crypto();
}

static bool crypto_random_values_is_integer_array(JsTypedArray* ta) {
    if (!ta) return false;
    switch (ta->element_type) {
        case JS_TYPED_INT8:
        case JS_TYPED_UINT8:
        case JS_TYPED_UINT8_CLAMPED:
        case JS_TYPED_INT16:
        case JS_TYPED_UINT16:
        case JS_TYPED_INT32:
        case JS_TYPED_UINT32:
        case JS_TYPED_BIGINT64:
        case JS_TYPED_BIGUINT64:
            return true;
        default:
            return false;
    }
}

extern "C" Item js_crypto_getRandomValues(Item target_item) {
    if (!js_is_typed_array(target_item)) {
        return js_throw_invalid_arg_type("array", "an integer-type TypedArray", target_item);
    }

    JsTypedArray* ta = js_get_typed_array_ptr(target_item.map);
    if (!crypto_random_values_is_integer_array(ta)) {
        return js_throw_type_error("The provided ArrayBufferView is not an integer typed array");
    }

    int byte_len = js_typed_array_byte_length(target_item);
    void* data = js_typed_array_current_data_ptr(target_item);
    if (!data && byte_len > 0) {
        return js_throw_type_error("Cannot get random values on an out-of-bounds TypedArray");
    }
    if (byte_len > 65536) {
        return js_throw_range_error("The ArrayBufferView's byte length exceeds 65536 bytes");
    }

    if (byte_len > 0 && !crypto_random_bytes((uint8_t*)data, (size_t)byte_len)) {
        log_error("crypto: getRandomValues: entropy source failed");
        return ItemNull;
    }
    return target_item;
}

extern "C" Item js_crypto_getFips(void) {
    return (Item){.item = i2it(0)};
}

// ============================================================================
// randomUUID/randomUUIDv7 → string "xxxxxxxx-xxxx-Vxxx-yxxx-xxxxxxxxxxxx"
// ============================================================================

static bool crypto_validate_uuid_options(Item options) {
    TypeId opt_type = get_type_id(options);
    if (opt_type != LMD_TYPE_UNDEFINED && opt_type != LMD_TYPE_NULL && opt_type != LMD_TYPE_MAP) {
        js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
            "The \"options\" argument must be of type object.");
        return false;
    }
    if (opt_type == LMD_TYPE_MAP) {
        Item dec = js_property_get(options, make_string_item_crypto("disableEntropyCache"));
        TypeId dec_type = get_type_id(dec);
        if (dec_type != LMD_TYPE_UNDEFINED && dec_type != LMD_TYPE_BOOL) {
            js_throw_type_error_code("ERR_INVALID_ARG_TYPE",
                "The \"options.disableEntropyCache\" property must be of type boolean.");
            return false;
        }
    }
    return true;
}

static void crypto_uuid_format_bytes(uint8_t bytes[16], char out[UUID_STR_LEN]) {
    int p = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[p++] = '-';
        out[p++] = hex_encode_nibble((unsigned)(bytes[i] >> 4));
        out[p++] = hex_encode_nibble((unsigned)(bytes[i] & 0x0F));
    }
    out[p] = '\0';
}

static uint64_t crypto_unix_time_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER uli;
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (uli.QuadPart - 116444736000000000ULL) / 10000ULL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
#endif
}

extern "C" Item js_crypto_randomUUID(Item options) {
    if (!crypto_validate_uuid_options(options)) return ItemNull;

    uint8_t bytes[16];
    if (!crypto_random_bytes(bytes, 16)) {
        log_error("crypto: randomUUID: entropy source failed");
        return ItemNull;
    }
    char uuid[UUID_STR_LEN];
    uuid_v4_format(bytes, uuid);
    return make_string_item_crypto(uuid);
}

extern "C" Item js_crypto_randomUUIDv7(Item options) {
    if (!crypto_validate_uuid_options(options)) return ItemNull;

    uint8_t bytes[16];
    if (!crypto_random_bytes(bytes, 16)) {
        log_error("crypto: randomUUIDv7: entropy source failed");
        return ItemNull;
    }

    uint64_t ts = crypto_unix_time_ms();
    bytes[0] = (uint8_t)(ts >> 40);
    bytes[1] = (uint8_t)(ts >> 32);
    bytes[2] = (uint8_t)(ts >> 24);
    bytes[3] = (uint8_t)(ts >> 16);
    bytes[4] = (uint8_t)(ts >> 8);
    bytes[5] = (uint8_t)ts;
    bytes[6] = (uint8_t)((bytes[6] & 0x0F) | 0x70);
    bytes[8] = (uint8_t)((bytes[8] & 0x3F) | 0x80);

    char uuid[UUID_STR_LEN];
    crypto_uuid_format_bytes(bytes, uuid);
    return make_string_item_crypto(uuid);
}

// ============================================================================
// randomInt([min,] max[, callback]) → integer in [min, max)
// ============================================================================

static const int64_t CRYPTO_RANDOM_INT_MAX_RANGE = 0xFFFFFFFFFFFFLL;
static const double CRYPTO_RANDOM_INT_MAX_SAFE = 9007199254740991.0;

static void crypto_format_int64_separated(int64_t value, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    char digits[64];
    snprintf(digits, sizeof(digits), "%lld", (long long)value);
    int len = (int)strlen(digits);
    int start = (digits[0] == '-') ? 1 : 0;
    int digit_count = len - start;
    int first_group = digit_count % 3;
    if (first_group == 0) first_group = 3;

    int pos = 0;
    if (start && pos < out_size - 1) out[pos++] = '-';
    for (int i = 0; i < digit_count && pos < out_size - 1; i++) {
        if (i > 0 && (i - first_group) % 3 == 0 && pos < out_size - 1) {
            out[pos++] = '_';
        }
        out[pos++] = digits[start + i];
    }
    out[pos] = '\0';
}

static Item crypto_throw_random_int_safe_integer(const char* name, Item actual) {
    char suffix[192];
    crypto_format_invalid_received(actual, suffix, sizeof(suffix));
    char msg[320];
    snprintf(msg, sizeof(msg),
        "The \"%s\" argument must be a safe integer.%s", name, suffix);
    return js_throw_type_error_code(JS_ERR_INVALID_ARG_TYPE, msg);
}

static bool crypto_item_to_safe_int(Item item, const char* name, int64_t* out_value) {
    if (!out_value) return false;
    TypeId type = get_type_id(item);
    double value = 0.0;
    if (type == LMD_TYPE_INT) {
        int64_t iv = it2i(item);
        if ((double)iv < -CRYPTO_RANDOM_INT_MAX_SAFE || (double)iv > CRYPTO_RANDOM_INT_MAX_SAFE) {
            crypto_throw_random_int_safe_integer(name, item);
            return false;
        }
        *out_value = iv;
        return true;
    }
    if (type == LMD_TYPE_INT64) {
        int64_t iv = it2l(item);
        if ((double)iv < -CRYPTO_RANDOM_INT_MAX_SAFE || (double)iv > CRYPTO_RANDOM_INT_MAX_SAFE) {
            crypto_throw_random_int_safe_integer(name, item);
            return false;
        }
        *out_value = iv;
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        value = it2d(item);
        if (value != value || value == 1.0 / 0.0 || value == -1.0 / 0.0 ||
            floor(value) != value ||
            value < -CRYPTO_RANDOM_INT_MAX_SAFE || value > CRYPTO_RANDOM_INT_MAX_SAFE) {
            crypto_throw_random_int_safe_integer(name, item);
            return false;
        }
        *out_value = (int64_t)value;
        return true;
    }

    crypto_throw_random_int_safe_integer(name, item);
    return false;
}

static Item crypto_throw_random_int_max_greater(int64_t min_val, int64_t max_val) {
    char msg[256];
    snprintf(msg, sizeof(msg),
        "The value of \"max\" is out of range. It must be greater than the value of \"min\" (%lld). Received %lld",
        (long long)min_val, (long long)max_val);
    return js_throw_range_error_code(JS_ERR_OUT_OF_RANGE, msg);
}

static Item crypto_throw_random_int_max_bound(int64_t max_val) {
    char max_buf[64];
    char actual_buf[64];
    snprintf(max_buf, sizeof(max_buf), "%lld", (long long)CRYPTO_RANDOM_INT_MAX_RANGE);
    crypto_format_int64_separated(max_val, actual_buf, sizeof(actual_buf));
    char msg[256];
    snprintf(msg, sizeof(msg),
        "The value of \"max\" is out of range. It must be <= %s. Received %s",
        max_buf, actual_buf);
    return js_throw_range_error_code(JS_ERR_OUT_OF_RANGE, msg);
}

static Item crypto_throw_random_int_range_bound(int64_t range) {
    char max_buf[64];
    char range_buf[64];
    snprintf(max_buf, sizeof(max_buf), "%lld", (long long)CRYPTO_RANDOM_INT_MAX_RANGE);
    crypto_format_int64_separated(range, range_buf, sizeof(range_buf));
    char msg[256];
    snprintf(msg, sizeof(msg),
        "The value of \"max - min\" is out of range. It must be <= %s. Received %s",
        max_buf, range_buf);
    return js_throw_range_error_code(JS_ERR_OUT_OF_RANGE, msg);
}

extern "C" Item js_crypto_randomInt(Item min_item, Item max_item, Item callback_item) {
    Item min_arg = (Item){.item = i2it(0)};
    Item max_arg = min_item;
    Item callback = callback_item;
    bool one_arg_form = true;

    if (get_type_id(max_item) == LMD_TYPE_FUNC && crypto_item_is_undefined(callback_item)) {
        callback = max_item;
    } else if (!crypto_item_is_undefined(max_item)) {
        min_arg = min_item;
        max_arg = max_item;
        one_arg_form = false;
    }

    bool has_callback = !crypto_item_is_undefined(callback);
    if (has_callback && get_type_id(callback) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "Function", callback);
    }

    int64_t min_val = 0;
    int64_t max_val = 0;
    if (!crypto_item_to_safe_int(min_arg, "min", &min_val)) return ItemNull;
    if (!crypto_item_to_safe_int(max_arg, "max", &max_val)) return ItemNull;

    if (max_val <= min_val) {
        return crypto_throw_random_int_max_greater(min_val, max_val);
    }

    int64_t range = max_val - min_val;
    if (one_arg_form && max_val > CRYPTO_RANDOM_INT_MAX_RANGE) {
        return crypto_throw_random_int_max_bound(max_val);
    }
    if (range > CRYPTO_RANDOM_INT_MAX_RANGE) {
        return crypto_throw_random_int_range_bound(range);
    }

    uint64_t rnd = 0;
    if (!crypto_random_bytes((uint8_t*)&rnd, sizeof(rnd))) {
        log_error("crypto: randomInt: entropy source failed");
        return ItemNull;
    }
    int64_t value = min_val + (int64_t)(rnd % (uint64_t)range);
    Item result = (Item){.item = i2it(value)};

    if (has_callback) {
        Item args[2] = { ItemNull, result };
        js_call_function(callback, ItemNull, args, 2);
        return make_js_undefined_crypto();
    }
    return result;
}

extern "C" Item js_crypto_argon2_unsupported(void) {
    return js_throw_error_with_code(JS_ERR_CRYPTO_ARGON2_NOT_SUPPORTED,
        "Argon2 is not supported by this crypto backend");
}

// ============================================================================
// HMAC — hash-based message authentication code (native implementation)
// ============================================================================

static void hmac_compute(const uint8_t* key, int key_len,
                         const uint8_t* data, int data_len,
                         const char* alg, uint8_t* out, int* out_len) {
    if (out_len) *out_len = 0;
    if (!out || !out_len) return;

    int bits = crypto_digest_bits_for_name_ext(alg, true, true, true);
    int hash_len = (int)digest_output_len_bits(bits);
    if (hash_len <= 0) return;

    if (key_len < 0) key_len = 0;
    if (data_len < 0) data_len = 0;
    const uint8_t* key_bytes = key ? key : crypto_empty_bytes;
    const uint8_t* data_bytes = data ? data : crypto_empty_bytes;
    if (!digest_hmac_compute_bits(bits, key_bytes, (size_t)key_len,
                                  data_bytes, (size_t)data_len,
                                  out, (size_t)hash_len)) {
        log_error("crypto: hmac compute failed for alg=%s", alg ? alg : "(null)");
        return;
    }
    *out_len = hash_len;
}

// ============================================================================
// Encoding helpers
// ============================================================================

static Item bytes_to_hex_string(const uint8_t* bytes, int len) {
    char* hex = (char*)mem_alloc((size_t)(len * 2 + 1), MEM_CAT_JS_RUNTIME);
    hex_encode(bytes, (size_t)len, hex);
    Item result = make_string_item_crypto(hex);
    mem_free(hex);
    return result;
}

static Item bytes_to_base64_string(const uint8_t* bytes, int len) {
    size_t out_len = base64_encoded_len((size_t)len, BASE64_STD);
    char* out = (char*)mem_alloc(out_len + 1, MEM_CAT_JS_RUNTIME);
    base64_encode(bytes, (size_t)len, out, BASE64_STD);
    Item result = make_string_item_crypto(out);
    mem_free(out);
    return result;
}

static void crypto_append_utf8_codepoint(char* out, int out_cap, int* pos, uint32_t cp) {
    if (!out || !pos || *pos >= out_cap - 1) return;
    if (cp < 0x80) {
        out[(*pos)++] = (char)cp;
    } else if (cp < 0x800 && *pos + 1 < out_cap - 1) {
        out[(*pos)++] = (char)(0xC0 | (cp >> 6));
        out[(*pos)++] = (char)(0x80 | (cp & 0x3F));
    } else if (*pos + 2 < out_cap - 1) {
        out[(*pos)++] = (char)(0xE0 | (cp >> 12));
        out[(*pos)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*pos)++] = (char)(0x80 | (cp & 0x3F));
    }
    out[*pos] = '\0';
}

static Item bytes_to_latin1_string(const uint8_t* bytes, int len) {
    if (len <= 0) return make_string_item_crypto("");
    char* out = (char*)mem_alloc((size_t)len * 2 + 1, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    for (int i = 0; i < len; i++) {
        crypto_append_utf8_codepoint(out, len * 2 + 1, &pos, bytes ? bytes[i] : 0);
    }
    Item result = make_string_item_crypto(out);
    mem_free(out);
    return result;
}

static Item bytes_to_ucs2_string(const uint8_t* bytes, int len) {
    if (len <= 1) return make_string_item_crypto("");
    char* out = (char*)mem_alloc((size_t)len * 2 + 1, MEM_CAT_JS_RUNTIME);
    int pos = 0;
    for (int i = 0; i + 1 < len; i += 2) {
        uint32_t cp = (uint32_t)(bytes ? bytes[i] : 0) | ((uint32_t)(bytes ? bytes[i + 1] : 0) << 8);
        crypto_append_utf8_codepoint(out, len * 2 + 1, &pos, cp);
    }
    Item result = make_string_item_crypto(out);
    mem_free(out);
    return result;
}

static bool crypto_normalize_string(Item item, char* out, int out_size, bool strip_dash) {
    if (!out || out_size <= 0 || get_type_id(item) != LMD_TYPE_STRING) return false;
    String* s = it2s(item);
    int pos = 0;
    for (int i = 0; s && i < (int)s->len && pos < out_size - 1; i++) {
        char c = s->chars[i];
        if (strip_dash && c == '-') continue;
        out[pos++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    out[pos] = '\0';
    return true;
}

static bool crypto_normalize_encoding(Item encoding_item, char* out, int out_size, bool* has_encoding) {
    if (!out || out_size <= 0) return false;
    out[0] = '\0';
    if (has_encoding) *has_encoding = false;

    TypeId type = get_type_id(encoding_item);
    if (type == LMD_TYPE_MAP) {
        encoding_item = js_to_string(encoding_item);
        if (js_check_exception()) return false;
        type = get_type_id(encoding_item);
    }

    if (crypto_item_is_undefined(encoding_item) || type == LMD_TYPE_NULL) return true;
    if (type != LMD_TYPE_STRING) return true;

    if (has_encoding) *has_encoding = true;
    return crypto_normalize_string(encoding_item, out, out_size, false);
}

static Item crypto_digest_output_for_encoding(const uint8_t* bytes, int len, const char* enc, bool has_encoding) {
    if (!has_encoding || !enc || enc[0] == '\0' || strcmp(enc, "buffer") == 0) {
        return crypto_buffer_from_bytes(bytes, len);
    }
    if (strcmp(enc, "hex") == 0) return bytes_to_hex_string(bytes, len);
    if (strcmp(enc, "base64") == 0) return bytes_to_base64_string(bytes, len);
    if (strcmp(enc, "latin1") == 0 || strcmp(enc, "binary") == 0) return bytes_to_latin1_string(bytes, len);
    if (strcmp(enc, "ucs2") == 0 || strcmp(enc, "ucs-2") == 0 ||
        strcmp(enc, "utf16le") == 0 || strcmp(enc, "utf-16le") == 0) {
        return bytes_to_ucs2_string(bytes, len);
    }
    if (strcmp(enc, "utf8") == 0 || strcmp(enc, "utf-8") == 0) {
        String* str = heap_create_name((const char*)(bytes ? bytes : crypto_empty_bytes), (size_t)(len > 0 ? len : 0));
        return (Item){.item = s2it(str)};
    }
    return crypto_buffer_from_bytes(bytes, len);
}

static Item crypto_empty_digest_output(Item encoding_item) {
    char enc[32];
    bool has_encoding = false;
    if (!crypto_normalize_encoding(encoding_item, enc, sizeof(enc), &has_encoding)) return ItemNull;
    return crypto_digest_output_for_encoding(crypto_empty_bytes, 0, enc, has_encoding);
}

static uint32_t crypto_next_utf8_codepoint(const char* str, int len, int* index) {
    if (!str || !index || *index >= len) return 0;
    unsigned char ch = (unsigned char)str[*index];
    if (ch < 0x80) {
        (*index)++;
        return ch;
    }
    if ((ch & 0xE0) == 0xC0 && *index + 1 < len) {
        uint32_t cp = (uint32_t)(ch & 0x1F);
        cp = (cp << 6) | (uint32_t)(str[*index + 1] & 0x3F);
        *index += 2;
        return cp;
    }
    if ((ch & 0xF0) == 0xE0 && *index + 2 < len) {
        uint32_t cp = (uint32_t)(ch & 0x0F);
        cp = (cp << 6) | (uint32_t)(str[*index + 1] & 0x3F);
        cp = (cp << 6) | (uint32_t)(str[*index + 2] & 0x3F);
        *index += 3;
        return cp;
    }
    if ((ch & 0xF8) == 0xF0 && *index + 3 < len) {
        uint32_t cp = (uint32_t)(ch & 0x07);
        cp = (cp << 6) | (uint32_t)(str[*index + 1] & 0x3F);
        cp = (cp << 6) | (uint32_t)(str[*index + 2] & 0x3F);
        cp = (cp << 6) | (uint32_t)(str[*index + 3] & 0x3F);
        *index += 4;
        return cp;
    }
    (*index)++;
    return ch;
}

static int crypto_utf8_codepoint_count(const char* str, int len) {
    int count = 0;
    int index = 0;
    while (index < len) {
        crypto_next_utf8_codepoint(str, len, &index);
        count++;
    }
    return count;
}

static bool crypto_string_bytes_for_encoding(String* s, const char* enc, bool has_encoding,
                                             uint8_t** out, int* out_len) {
    if (!out || !out_len) return false;
    *out = NULL;
    *out_len = 0;
    if (!s) return false;

    if (!has_encoding || !enc || enc[0] == '\0' ||
        strcmp(enc, "utf8") == 0 || strcmp(enc, "utf-8") == 0) {
        size_t alloc_len = s->len > 0 ? s->len : 1;
        *out = (uint8_t*)mem_alloc(alloc_len, MEM_CAT_JS_RUNTIME);
        if (s->len > 0) memcpy(*out, s->chars, s->len);
        *out_len = (int)s->len;
        return true;
    }

    if (strcmp(enc, "base64") == 0 || strcmp(enc, "base64url") == 0) {
        size_t decoded_len = 0;
        uint8_t* decoded = base64_decode_variant(s->chars, s->len, &decoded_len,
            strcmp(enc, "base64url") == 0 ? BASE64_URL : BASE64_STD);
        if (!decoded && decoded_len == 0 && s->len > 0) return false;
        *out = decoded ? decoded : (uint8_t*)mem_alloc(1, MEM_CAT_JS_RUNTIME);
        *out_len = (int)decoded_len;
        return true;
    }

    if (strcmp(enc, "hex") == 0) {
        size_t bytes_len = s->len / 2;
        *out = (uint8_t*)mem_alloc(bytes_len > 0 ? bytes_len : 1, MEM_CAT_JS_RUNTIME);
        size_t written = 0;
        if (!hex_decode(s->chars, s->len, *out, &written)) {
            mem_free(*out);
            *out = NULL;
            return false;
        }
        *out_len = (int)written;
        return true;
    }

    if (strcmp(enc, "latin1") == 0 || strcmp(enc, "binary") == 0 ||
        strcmp(enc, "ascii") == 0) {
        int count = crypto_utf8_codepoint_count(s->chars, (int)s->len);
        *out = (uint8_t*)mem_alloc(count > 0 ? (size_t)count : 1, MEM_CAT_JS_RUNTIME);
        int index = 0;
        for (int i = 0; i < count; i++) {
            uint32_t cp = crypto_next_utf8_codepoint(s->chars, (int)s->len, &index);
            (*out)[i] = (uint8_t)(strcmp(enc, "ascii") == 0 ? (cp & 0x7F) : (cp & 0xFF));
        }
        *out_len = count;
        return true;
    }

    if (strcmp(enc, "ucs2") == 0 || strcmp(enc, "ucs-2") == 0 ||
        strcmp(enc, "utf16le") == 0 || strcmp(enc, "utf-16le") == 0) {
        int count = crypto_utf8_codepoint_count(s->chars, (int)s->len);
        *out = (uint8_t*)mem_alloc(count > 0 ? (size_t)(count * 2) : 1, MEM_CAT_JS_RUNTIME);
        int index = 0;
        for (int i = 0; i < count; i++) {
            uint32_t cp = crypto_next_utf8_codepoint(s->chars, (int)s->len, &index);
            (*out)[i * 2] = (uint8_t)(cp & 0xFF);
            (*out)[i * 2 + 1] = (uint8_t)((cp >> 8) & 0xFF);
        }
        *out_len = count * 2;
        return true;
    }

    return false;
}

// ============================================================================
// createHmac(algorithm, key) → object with update(data)/digest(encoding)
// ============================================================================

struct HmacCtx {
    char alg[16];
    uint8_t* key;
    int key_len;
    uint8_t* data;
    int data_len;
    int data_cap;
};

static void hmac_ctx_free(HmacCtx* ctx) {
    if (!ctx) return;
    if (ctx->key) mem_free(ctx->key);
    if (ctx->data) mem_free(ctx->data);
    mem_free(ctx);
}

static void hmac_ctx_append(HmacCtx* ctx, const uint8_t* buf, int len) {
    if (!ctx || len <= 0) return;
    int need = ctx->data_len + len;
    if (need > ctx->data_cap) {
        int cap = ctx->data_cap == 0 ? 1024 : ctx->data_cap;
        while (cap < need) cap *= 2;
        ctx->data = (uint8_t*)mem_realloc(ctx->data, (size_t)cap, MEM_CAT_JS_RUNTIME);
        ctx->data_cap = cap;
    }
    memcpy(ctx->data + ctx->data_len, buf, (size_t)len);
    ctx->data_len += len;
}

static void crypto_link_instance_to_constructor(Item obj, const char* constructor_name) {
    if (get_type_id(obj) != LMD_TYPE_MAP || get_type_id(crypto_namespace) != LMD_TYPE_MAP) return;
    Item ctor = js_property_get(crypto_namespace, make_string_item_crypto(constructor_name));
    if (get_type_id(ctor) != LMD_TYPE_FUNC) return;
    Item proto = js_property_get(ctor, make_string_item_crypto("prototype"));
    if (get_type_id(proto) == LMD_TYPE_MAP) js_set_prototype(obj, proto);
}

extern "C" Item js_hmac_update(Item data_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__hmac_ctx__"));
    if (ctx_item.item == 0) return self;
    HmacCtx* ctx = (HmacCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx) return self;

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        hmac_ctx_append(ctx, (const uint8_t*)s->chars, (int)s->len);
    } else if (js_is_typed_array(data_item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(data_item, &buf, &len)) hmac_ctx_append(ctx, buf, len);
    } else if (js_is_dataview(data_item) || js_is_arraybuffer(data_item)) {
        uint8_t* bytes = NULL;
        int len = 0;
        if (extract_bytes(data_item, &bytes, &len)) {
            hmac_ctx_append(ctx, bytes, len);
            mem_free(bytes);
        }
    }
    return self;
}

extern "C" Item js_hmac_digest(Item encoding_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__hmac_ctx__"));
    if (ctx_item.item == 0 || ctx_item.item == ITEM_NULL) return crypto_empty_digest_output(encoding_item);
    HmacCtx* ctx = (HmacCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx) return crypto_empty_digest_output(encoding_item);

    char enc[32];
    bool has_encoding = false;
    if (!crypto_normalize_encoding(encoding_item, enc, sizeof(enc), &has_encoding)) {
        hmac_ctx_free(ctx);
        js_property_set(self, make_string_item_crypto("__hmac_ctx__"), ItemNull);
        return ItemNull;
    }

    uint8_t hash[64];
    int hash_len = 0;
    hmac_compute(ctx->key, ctx->key_len, ctx->data, ctx->data_len, ctx->alg, hash, &hash_len);

    hmac_ctx_free(ctx);
    js_property_set(self, make_string_item_crypto("__hmac_ctx__"), ItemNull);

    if (hash_len == 0) return crypto_empty_digest_output(encoding_item);
    return crypto_digest_output_for_encoding(hash, hash_len, enc, has_encoding);
}

extern "C" Item js_hmac_end(Item data_item) {
    Item self = js_get_current_this();
    if (!crypto_item_is_undefined(data_item) && data_item.item != ITEM_NULL) {
        js_hmac_update(data_item);
        if (js_check_exception()) return ItemNull;
    }
    Item digest = js_hmac_digest(make_string_item_crypto("buffer"));
    if (js_check_exception()) return ItemNull;
    js_property_set(self, make_string_item_crypto("__hmac_read__"), digest);
    return self;
}

extern "C" Item js_hmac_read(void) {
    Item self = js_get_current_this();
    Item digest = js_property_get(self, make_string_item_crypto("__hmac_read__"));
    if (digest.item == 0 || digest.item == ITEM_NULL) return ItemNull;
    js_property_set(self, make_string_item_crypto("__hmac_read__"), ItemNull);
    return digest;
}

extern "C" Item js_crypto_createHmac(Item alg_item, Item key_item) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) return js_throw_invalid_arg_type("hmac", "string", alg_item);

    char alg_buf[16];
    crypto_normalize_string(alg_item, alg_buf, sizeof(alg_buf), true);
    if (crypto_digest_bits_for_name_ext(alg_buf, true, true, true) == 0) {
        char msg[128];
        String* alg = it2s(alg_item);
        snprintf(msg, sizeof(msg), "Invalid digest: %.*s", (int)alg->len, alg->chars);
        return js_throw_type_error(msg);
    }

    uint8_t* key = NULL;
    int key_len = 0;
    if (!extract_bytes(key_item, &key, &key_len)) {
        return js_throw_invalid_arg_type("key", "string, ArrayBuffer, Buffer, TypedArray, DataView, or KeyObject", key_item);
    }

    HmacCtx* ctx = (HmacCtx*)mem_calloc(1, sizeof(HmacCtx), MEM_CAT_JS_RUNTIME);
    memcpy(ctx->alg, alg_buf, strlen(alg_buf) + 1);
    ctx->key = key;
    ctx->key_len = key_len;

    Item obj = js_new_object();
    crypto_link_instance_to_constructor(obj, "Hmac");
    js_property_set(obj, make_string_item_crypto("__hmac_ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    js_property_set(obj, make_string_item_crypto("update"),
                    js_new_function((void*)js_hmac_update, 1));
    js_property_set(obj, make_string_item_crypto("digest"),
                    js_new_function((void*)js_hmac_digest, 1));
    js_property_set(obj, make_string_item_crypto("end"),
                    js_new_function((void*)js_hmac_end, 1));
    js_property_set(obj, make_string_item_crypto("read"),
                    js_new_function((void*)js_hmac_read, 0));
    return obj;
}

// ============================================================================
// createHash(algorithm) → object with update(data)/digest(encoding)
// ============================================================================

struct HashCtx {
    char alg[16];
    uint8_t* data;
    int data_len;
    int data_cap;
    bool finalized;
};

static void hash_ctx_free(HashCtx* ctx) {
    if (!ctx) return;
    if (ctx->data) mem_free(ctx->data);
    mem_free(ctx);
}

static void hash_ctx_append(HashCtx* ctx, const uint8_t* buf, int len) {
    if (!ctx || len <= 0) return;
    int need = ctx->data_len + len;
    if (need > ctx->data_cap) {
        int cap = ctx->data_cap == 0 ? 1024 : ctx->data_cap;
        while (cap < need) cap *= 2;
        ctx->data = (uint8_t*)mem_realloc(ctx->data, (size_t)cap, MEM_CAT_JS_RUNTIME);
        ctx->data_cap = cap;
    }
    memcpy(ctx->data + ctx->data_len, buf, (size_t)len);
    ctx->data_len += len;
}

static Item crypto_throw_hash_finalized(void) {
    return js_throw_error_with_code("ERR_CRYPTO_HASH_FINALIZED", "Digest already called");
}

extern "C" Item js_hash_update(Item data_item, Item encoding_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__hash_ctx__"));
    if (ctx_item.item == 0 || ctx_item.item == ITEM_NULL) return crypto_throw_hash_finalized();
    HashCtx* ctx = (HashCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx || ctx->finalized) return crypto_throw_hash_finalized();

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        char enc[32];
        bool has_encoding = false;
        if (!crypto_normalize_encoding(encoding_item, enc, sizeof(enc), &has_encoding)) return ItemNull;
        uint8_t* bytes = NULL;
        int len = 0;
        if (!crypto_string_bytes_for_encoding(s, enc, has_encoding, &bytes, &len)) {
            return js_throw_invalid_arg_value("encoding", "is invalid", encoding_item);
        }
        hash_ctx_append(ctx, bytes, len);
        mem_free(bytes);
    } else if (js_is_typed_array(data_item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(data_item, &buf, &len)) hash_ctx_append(ctx, buf, len);
    } else if (js_is_dataview(data_item) || js_is_arraybuffer(data_item)) {
        uint8_t* bytes = NULL;
        int len = 0;
        if (extract_bytes(data_item, &bytes, &len)) {
            hash_ctx_append(ctx, bytes, len);
            mem_free(bytes);
        }
    } else {
        return js_throw_invalid_arg_type("data", "string, ArrayBuffer, Buffer, TypedArray, or DataView", data_item);
    }
    return self;
}

extern "C" Item js_hash_digest(Item encoding_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__hash_ctx__"));
    if (ctx_item.item == 0 || ctx_item.item == ITEM_NULL) return crypto_throw_hash_finalized();
    HashCtx* ctx = (HashCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx || ctx->finalized) return crypto_throw_hash_finalized();

    char enc[32];
    bool has_encoding = false;
    if (!crypto_normalize_encoding(encoding_item, enc, sizeof(enc), &has_encoding)) return ItemNull;

    uint8_t hash[64];
    int hash_len = 0;

    int digest_bits = crypto_digest_bits_for_name_ext(ctx->alg, true, true, true);
    hash_len = (int)digest_output_len_bits(digest_bits);
    if (hash_len > 0) {
        if (!crypto_digest_compute_bits(digest_bits, ctx->data, 0, ctx->data_len, hash)) {
            hash_len = 0;
        }
    }

    hash_ctx_free(ctx);
    js_property_set(self, make_string_item_crypto("__hash_ctx__"), ItemNull);

    if (hash_len == 0) return crypto_empty_digest_output(encoding_item);
    return crypto_digest_output_for_encoding(hash, hash_len, enc, has_encoding);
}

extern "C" Item js_hash_end(Item data_item) {
    Item self = js_get_current_this();
    if (!crypto_item_is_undefined(data_item) && data_item.item != ITEM_NULL) {
        js_hash_update(data_item, make_js_undefined_crypto());
        if (js_check_exception()) return ItemNull;
    }
    Item digest = js_hash_digest(make_string_item_crypto("buffer"));
    if (js_check_exception()) return ItemNull;
    js_property_set(self, make_string_item_crypto("__hash_read__"), digest);
    return self;
}

extern "C" Item js_hash_read(void) {
    Item self = js_get_current_this();
    Item digest = js_property_get(self, make_string_item_crypto("__hash_read__"));
    if (digest.item == 0 || digest.item == ITEM_NULL) return ItemNull;
    js_property_set(self, make_string_item_crypto("__hash_read__"), ItemNull);
    return digest;
}

extern "C" Item js_crypto_createHash(Item alg_item) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) return js_throw_invalid_arg_type("algorithm", "string", alg_item);
    char alg_buf[16];
    crypto_normalize_string(alg_item, alg_buf, sizeof(alg_buf), true);
    if (crypto_digest_bits_for_name_ext(alg_buf, true, true, true) == 0) {
        return js_throw_type_error("Digest method not supported");
    }

    HashCtx* ctx = (HashCtx*)mem_calloc(1, sizeof(HashCtx), MEM_CAT_JS_RUNTIME);
    memcpy(ctx->alg, alg_buf, strlen(alg_buf) + 1);

    Item obj = js_new_object();
    crypto_link_instance_to_constructor(obj, "Hash");
    js_property_set(obj, make_string_item_crypto("__hash_ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    js_property_set(obj, make_string_item_crypto("update"),
                    js_new_function((void*)js_hash_update, 2));
    js_property_set(obj, make_string_item_crypto("write"),
                    js_new_function((void*)js_hash_update, 2));
    js_property_set(obj, make_string_item_crypto("digest"),
                    js_new_function((void*)js_hash_digest, 1));
    js_property_set(obj, make_string_item_crypto("end"),
                    js_new_function((void*)js_hash_end, 1));
    js_property_set(obj, make_string_item_crypto("read"),
                    js_new_function((void*)js_hash_read, 0));
    return obj;
}

// ============================================================================
// createSign/createVerify — streaming update surface
// ============================================================================

struct SignVerifyCtx {
    char alg[16];
    bool verify_mode;
    bool finalized;
    uint8_t* data;
    int data_len;
    int data_cap;
};

static void sign_verify_ctx_free(SignVerifyCtx* ctx) {
    if (!ctx) return;
    if (ctx->data) mem_free(ctx->data);
    mem_free(ctx);
}

static void sign_verify_ctx_append(SignVerifyCtx* ctx, const uint8_t* buf, int len) {
    if (!ctx || len <= 0) return;
    int need = ctx->data_len + len;
    if (need > ctx->data_cap) {
        int cap = ctx->data_cap == 0 ? 1024 : ctx->data_cap;
        while (cap < need) cap *= 2;
        ctx->data = (uint8_t*)mem_realloc(ctx->data, (size_t)cap, MEM_CAT_JS_RUNTIME);
        ctx->data_cap = cap;
    }
    memcpy(ctx->data + ctx->data_len, buf, (size_t)len);
    ctx->data_len += len;
}

static Item crypto_throw_sign_verify_finalized(void) {
    return js_throw_error_with_code(JS_ERR_CRYPTO_INVALID_STATE, "Sign or Verify already finalized");
}

static SignVerifyCtx* sign_verify_ctx_from_this(Item self) {
    Item ctx_item = js_property_get(self, make_string_item_crypto("__sign_verify_ctx__"));
    if (ctx_item.item == 0 || ctx_item.item == ITEM_NULL) return NULL;
    return (SignVerifyCtx*)(uintptr_t)it2i(ctx_item);
}

extern "C" Item js_sign_verify_update(Item data_item, Item encoding_item) {
    Item self = js_get_current_this();
    SignVerifyCtx* ctx = sign_verify_ctx_from_this(self);
    if (!ctx || ctx->finalized) return crypto_throw_sign_verify_finalized();

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        char enc[32];
        bool has_encoding = false;
        if (!crypto_normalize_encoding(encoding_item, enc, sizeof(enc), &has_encoding)) return ItemNull;
        uint8_t* bytes = NULL;
        int len = 0;
        if (!crypto_string_bytes_for_encoding(s, enc, has_encoding, &bytes, &len)) {
            return js_throw_invalid_arg_value("encoding", "is invalid", encoding_item);
        }
        sign_verify_ctx_append(ctx, bytes, len);
        mem_free(bytes);
    } else if (js_is_typed_array(data_item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(data_item, &buf, &len)) sign_verify_ctx_append(ctx, buf, len);
    } else if (js_is_dataview(data_item) || js_is_arraybuffer(data_item)) {
        uint8_t* bytes = NULL;
        int len = 0;
        if (extract_bytes(data_item, &bytes, &len)) {
            sign_verify_ctx_append(ctx, bytes, len);
            mem_free(bytes);
        }
    } else {
        return js_throw_invalid_arg_type("data", "string, ArrayBuffer, Buffer, TypedArray, or DataView", data_item);
    }
    return self;
}

extern "C" Item js_sign_verify_end(Item data_item) {
    if (!crypto_item_is_undefined(data_item) && data_item.item != ITEM_NULL) {
        js_sign_verify_update(data_item, make_js_undefined_crypto());
        if (js_check_exception()) return ItemNull;
    }
    return js_get_current_this();
}

extern "C" Item js_sign_verify_sign(Item key_item, Item encoding_item) {
    (void)encoding_item;
    Item self = js_get_current_this();
    SignVerifyCtx* ctx = sign_verify_ctx_from_this(self);
    if (!ctx || ctx->finalized) return crypto_throw_sign_verify_finalized();
    if (ctx->verify_mode) return js_throw_type_error("Not a Sign object");
    if (crypto_item_is_undefined(key_item) || key_item.item == ITEM_NULL) {
        return js_throw_error_with_code(JS_ERR_CRYPTO_SIGN_KEY_REQUIRED, "No key provided to sign");
    }

    ctx->finalized = true;
    js_property_set(self, make_string_item_crypto("__sign_verify_ctx__"), ItemNull);
    sign_verify_ctx_free(ctx);
    return js_throw_error_with_code(JS_ERR_METHOD_NOT_IMPLEMENTED,
        "Asymmetric signing is not supported by this crypto backend");
}

extern "C" Item js_sign_verify_verify(Item key_item, Item signature_item, Item encoding_item) {
    Item self = js_get_current_this();
    SignVerifyCtx* ctx = sign_verify_ctx_from_this(self);
    if (!ctx || ctx->finalized) return crypto_throw_sign_verify_finalized();
    if (!ctx->verify_mode) return js_throw_type_error("Not a Verify object");

    if (crypto_item_is_undefined(key_item) || key_item.item == ITEM_NULL) {
        return js_throw_invalid_arg_type("key", "string, ArrayBuffer, Buffer, TypedArray, DataView, or KeyObject", key_item);
    }
    if (crypto_item_is_undefined(signature_item) || signature_item.item == ITEM_NULL) {
        return js_throw_invalid_arg_type("signature", "string, ArrayBuffer, Buffer, TypedArray, or DataView", signature_item);
    }

    if (get_type_id(signature_item) == LMD_TYPE_STRING) {
        String* s = it2s(signature_item);
        char enc[32];
        bool has_encoding = false;
        if (!crypto_normalize_encoding(encoding_item, enc, sizeof(enc), &has_encoding)) return ItemNull;
        uint8_t* bytes = NULL;
        int len = 0;
        if (!crypto_string_bytes_for_encoding(s, enc, has_encoding, &bytes, &len)) {
            return js_throw_invalid_arg_value("encoding", "is invalid", encoding_item);
        }
        mem_free(bytes);
    } else {
        uint8_t* bytes = NULL;
        int len = 0;
        if (!extract_bytes(signature_item, &bytes, &len)) {
            return js_throw_invalid_arg_type("signature", "string, ArrayBuffer, Buffer, TypedArray, or DataView", signature_item);
        }
        mem_free(bytes);
    }

    ctx->finalized = true;
    js_property_set(self, make_string_item_crypto("__sign_verify_ctx__"), ItemNull);
    sign_verify_ctx_free(ctx);
    return (Item){.item = b2it(false)};
}

static Item js_crypto_create_sign_verify(Item alg_item, bool verify_mode) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) return js_throw_invalid_arg_type("algorithm", "string", alg_item);

    char alg_buf[16];
    if (!crypto_normalize_sign_verify_digest(alg_item, alg_buf, sizeof(alg_buf))) {
        return js_throw_type_error("Digest method not supported");
    }

    SignVerifyCtx* ctx = (SignVerifyCtx*)mem_calloc(1, sizeof(SignVerifyCtx), MEM_CAT_JS_RUNTIME);
    memcpy(ctx->alg, alg_buf, strlen(alg_buf) + 1);
    ctx->verify_mode = verify_mode;

    Item obj = js_new_object();
    crypto_link_instance_to_constructor(obj, verify_mode ? "Verify" : "Sign");
    js_property_set(obj, make_string_item_crypto("__sign_verify_ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    js_property_set(obj, make_string_item_crypto("update"),
                    js_new_function((void*)js_sign_verify_update, 2));
    js_property_set(obj, make_string_item_crypto("write"),
                    js_new_function((void*)js_sign_verify_update, 2));
    js_property_set(obj, make_string_item_crypto("end"),
                    js_new_function((void*)js_sign_verify_end, 1));
    js_property_set(obj, make_string_item_crypto("sign"),
                    js_new_function((void*)js_sign_verify_sign, 2));
    js_property_set(obj, make_string_item_crypto("verify"),
                    js_new_function((void*)js_sign_verify_verify, 3));
    return obj;
}

extern "C" Item js_crypto_createSign(Item alg_item) {
    return js_crypto_create_sign_verify(alg_item, false);
}

extern "C" Item js_crypto_createVerify(Item alg_item) {
    return js_crypto_create_sign_verify(alg_item, true);
}

// crypto.hash(algorithm, data[, outputEncoding]) → digest
extern "C" Item js_crypto_hash(Item alg_item, Item data_item, Item encoding_item) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("algorithm", "string", alg_item);
    }

    String* alg = it2s(alg_item);
    char alg_buf[32];
    int pos = 0;
    for (int i = 0; alg && i < (int)alg->len && pos < (int)sizeof(alg_buf) - 1; i++) {
        char c = alg->chars[i];
        if (c == '-') continue;
        alg_buf[pos++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    alg_buf[pos] = '\0';

    int digest_bits = crypto_digest_bits_for_name_ext(alg_buf, true, true, true);
    int hash_len = (int)digest_output_len_bits(digest_bits);
    if (hash_len <= 0 || hash_len > 64) {
        return js_throw_invalid_arg_value("algorithm", "is invalid", alg_item);
    }

    const char* enc = "hex";
    char enc_buf[32];
    if (!crypto_item_is_undefined(encoding_item)) {
        if (get_type_id(encoding_item) != LMD_TYPE_STRING) {
            return js_throw_invalid_arg_type("outputEncoding", "string", encoding_item);
        }
        String* s = it2s(encoding_item);
        int len = (int)s->len < (int)sizeof(enc_buf) - 1 ? (int)s->len : (int)sizeof(enc_buf) - 1;
        memcpy(enc_buf, s->chars, (size_t)len);
        enc_buf[len] = '\0';
        enc = enc_buf;
        if (strcmp(enc, "hex") != 0 && strcmp(enc, "base64") != 0 && strcmp(enc, "buffer") != 0) {
            return js_throw_invalid_arg_value("outputEncoding", "is invalid", encoding_item);
        }
    }

    uint8_t* data = NULL;
    int data_len = 0;
    if (!extract_bytes(data_item, &data, &data_len)) {
        return js_throw_invalid_arg_type("data", "string, ArrayBuffer, Buffer, TypedArray, or DataView", data_item);
    }

    uint8_t hash[64];
    bool ok = crypto_digest_compute_bits(digest_bits, data, 0, data_len, hash);
    mem_free(data);
    if (!ok) return ItemNull;

    if (strcmp(enc, "hex") == 0) return bytes_to_hex_string(hash, hash_len);
    if (strcmp(enc, "base64") == 0) return bytes_to_base64_string(hash, hash_len);

    Item result = js_typed_array_new(JS_TYPED_UINT8, hash_len);
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (ta && ta->data) memcpy(ta->data, hash, (size_t)hash_len);
    return result;
}

// ============================================================================
// getHashes() → array of supported algorithm names
// ============================================================================

extern "C" Item js_crypto_getHashes(void) {
    Item arr = js_array_new(0);
    js_array_push(arr, make_string_item_crypto("sha256"));
    js_array_push(arr, make_string_item_crypto("sha384"));
    js_array_push(arr, make_string_item_crypto("sha512"));
    return arr;
}

// ============================================================================
// Diffie-Hellman / ECDH constructor surfaces
// ============================================================================

static const char CRYPTO_DH_MODP2_HEX[] =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381"
    "FFFFFFFFFFFFFFFF";

static const char CRYPTO_DH_MODP5_HEX[] =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
    "670C354E4ABC9804F1746C08CA237327FFFFFFFFFFFFFFFF";

struct CryptoDhGroupDef {
    const char* name;
    const char* prime_hex;
};

static const CryptoDhGroupDef crypto_dh_groups[] = {
    {"modp2", CRYPTO_DH_MODP2_HEX},
    {"modp5", CRYPTO_DH_MODP5_HEX},
    {NULL, NULL}
};

static bool crypto_hex_const_to_bytes(const char* hex, uint8_t** out, int* out_len) {
    if (!hex || !out || !out_len) return false;
    size_t hex_len = strlen(hex);
    size_t bytes_len = hex_len / 2;
    uint8_t* bytes = (uint8_t*)mem_alloc(bytes_len > 0 ? bytes_len : 1, MEM_CAT_JS_RUNTIME);
    size_t written = 0;
    if (!hex_decode(hex, hex_len, bytes, &written)) {
        mem_free(bytes);
        return false;
    }
    *out = bytes;
    *out_len = (int)written;
    return true;
}

static const CryptoDhGroupDef* crypto_dh_group_lookup(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return NULL;
    char name[32];
    if (!crypto_normalize_string(name_item, name, sizeof(name), false)) return NULL;
    for (int i = 0; crypto_dh_groups[i].name; i++) {
        if (strcmp(name, crypto_dh_groups[i].name) == 0) return &crypto_dh_groups[i];
    }
    return NULL;
}

static Item crypto_throw_unknown_dh_group(void) {
    return js_throw_error_with_code("ERR_CRYPTO_UNKNOWN_DH_GROUP", "Unknown DH group");
}

static Item crypto_throw_bad_dh_generator(void) {
    return js_throw_error_with_code("ERR_OSSL_DH_BAD_GENERATOR", "bad generator");
}

static Item crypto_throw_dh_not_implemented(void) {
    return js_throw_error_with_code(JS_ERR_METHOD_NOT_IMPLEMENTED,
        "Diffie-Hellman key agreement is not supported by this crypto backend yet");
}

static bool crypto_item_to_integer(Item item, const char* name, int* out_value) {
    if (!out_value) return false;
    TypeId type = get_type_id(item);
    double value = 0.0;
    if (type == LMD_TYPE_INT) {
        int64_t iv = it2i(item);
        if (iv <= -(int64_t)JS_SYMBOL_BASE) {
            return js_throw_invalid_arg_type(name, "number", item), false;
        }
        if (iv < -2147483648LL || iv > 2147483647LL) {
            return js_throw_out_of_range(name, ">= -2147483648 && <= 2147483647", item), false;
        }
        *out_value = (int)iv;
        return true;
    }
    if (type == LMD_TYPE_INT64) {
        int64_t iv = it2l(item);
        if (iv < -2147483648LL || iv > 2147483647LL) {
            return js_throw_out_of_range(name, ">= -2147483648 && <= 2147483647", item), false;
        }
        *out_value = (int)iv;
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        value = it2d(item);
        if (value != value || floor(value) != value ||
                value < -2147483648.0 || value > 2147483647.0) {
            return js_throw_out_of_range(name, "an integer", item), false;
        }
        *out_value = (int)value;
        return true;
    }
    return js_throw_invalid_arg_type(name, "number", item), false;
}

static bool crypto_extract_bytes_with_encoding(Item item, Item encoding_item, uint8_t** out, int* out_len) {
    if (get_type_id(item) == LMD_TYPE_STRING) {
        char enc[32];
        bool has_encoding = false;
        if (!crypto_normalize_encoding(encoding_item, enc, sizeof(enc), &has_encoding)) return false;
        String* s = it2s(item);
        if (!crypto_string_bytes_for_encoding(s, enc, has_encoding, out, out_len)) {
            js_throw_invalid_arg_value("encoding", "is invalid", encoding_item);
            return false;
        }
        return true;
    }
    return extract_bytes(item, out, out_len);
}

static bool crypto_generator_bytes_from_int(int generator, uint8_t** out, int* out_len) {
    if (!out || !out_len) return false;
    if (generator == 0) generator = 2;
    if (generator <= 1) {
        crypto_throw_bad_dh_generator();
        return false;
    }

    uint8_t buf[4];
    buf[0] = (uint8_t)((generator >> 24) & 0xFF);
    buf[1] = (uint8_t)((generator >> 16) & 0xFF);
    buf[2] = (uint8_t)((generator >> 8) & 0xFF);
    buf[3] = (uint8_t)(generator & 0xFF);
    int start = 0;
    while (start < 3 && buf[start] == 0) start++;
    int len = 4 - start;
    *out = (uint8_t*)mem_alloc((size_t)len, MEM_CAT_JS_RUNTIME);
    memcpy(*out, buf + start, (size_t)len);
    *out_len = len;
    return true;
}

static bool crypto_validate_generator_bytes(const uint8_t* bytes, int len) {
    if (!bytes || len <= 0) {
        crypto_throw_bad_dh_generator();
        return false;
    }
    int first_non_zero = 0;
    while (first_non_zero < len && bytes[first_non_zero] == 0) first_non_zero++;
    if (first_non_zero >= len) {
        crypto_throw_bad_dh_generator();
        return false;
    }
    if (first_non_zero == len - 1 && bytes[first_non_zero] <= 1) {
        crypto_throw_bad_dh_generator();
        return false;
    }
    return true;
}

static bool crypto_parse_dh_generator(Item generator_item, Item encoding_item, uint8_t** out, int* out_len) {
    if (!out || !out_len) return false;
    *out = NULL;
    *out_len = 0;
    if (crypto_item_is_undefined(generator_item) || get_type_id(generator_item) == LMD_TYPE_NULL) {
        return crypto_generator_bytes_from_int(2, out, out_len);
    }

    TypeId type = get_type_id(generator_item);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64 || type == LMD_TYPE_FLOAT) {
        int generator = 0;
        if (!crypto_item_to_integer(generator_item, "generator", &generator)) return false;
        return crypto_generator_bytes_from_int(generator, out, out_len);
    }

    if (!crypto_extract_bytes_with_encoding(generator_item, encoding_item, out, out_len)) {
        js_throw_invalid_arg_type("generator", "number, string, ArrayBuffer, Buffer, TypedArray, or DataView", generator_item);
        return false;
    }
    if (!crypto_validate_generator_bytes(*out, *out_len)) {
        mem_free(*out);
        *out = NULL;
        *out_len = 0;
        return false;
    }
    return true;
}

static int crypto_mbedtls_random(void* ctx, unsigned char* output, size_t len) {
    (void)ctx;
    return crypto_random_bytes(output, len) ? 0 : -1;
}

static bool crypto_generate_dh_prime(int bits, uint8_t** out, int* out_len) {
    if (!out || !out_len) return false;
    *out = NULL;
    *out_len = 0;
    if (bits < 2) {
        return js_throw_error_with_code("ERR_OSSL_BN_BITS_TOO_SMALL", "bits too small"), false;
    }

    mbedtls_mpi prime;
    mbedtls_mpi_init(&prime);
    int ret = mbedtls_mpi_gen_prime(&prime, (size_t)bits, MBEDTLS_MPI_GEN_PRIME_FLAG_DH,
        crypto_mbedtls_random, NULL);
    if (ret != 0) {
        mbedtls_mpi_free(&prime);
        log_error("crypto: DH prime generation failed: -0x%04x", -ret);
        return js_throw_error_with_code("ERR_OSSL_DH_GENERATE_PRIME_FAILED", "Failed to generate DH prime"), false;
    }

    size_t len = mbedtls_mpi_size(&prime);
    uint8_t* bytes = (uint8_t*)mem_alloc(len > 0 ? len : 1, MEM_CAT_JS_RUNTIME);
    ret = mbedtls_mpi_write_binary(&prime, bytes, len);
    mbedtls_mpi_free(&prime);
    if (ret != 0) {
        mem_free(bytes);
        log_error("crypto: DH prime export failed: -0x%04x", -ret);
        return js_throw_error_with_code("ERR_OSSL_DH_GENERATE_PRIME_FAILED", "Failed to export DH prime"), false;
    }

    *out = bytes;
    *out_len = (int)len;
    return true;
}

static Item crypto_output_object_bytes(Item self, const char* prop_name, Item encoding_item) {
    Item bytes_item = js_property_get(self, make_string_item_crypto(prop_name));
    uint8_t* bytes = NULL;
    int len = 0;
    if (!extract_bytes(bytes_item, &bytes, &len)) return ItemNull;
    char enc[32];
    bool has_encoding = false;
    if (!crypto_normalize_encoding(encoding_item, enc, sizeof(enc), &has_encoding)) {
        mem_free(bytes);
        return ItemNull;
    }
    Item result = crypto_digest_output_for_encoding(bytes, len, enc, has_encoding);
    mem_free(bytes);
    return result;
}

extern "C" Item js_dh_getPrime(Item encoding_item) {
    return crypto_output_object_bytes(js_get_current_this(), "__dh_prime__", encoding_item);
}

extern "C" Item js_dh_getGenerator(Item encoding_item) {
    return crypto_output_object_bytes(js_get_current_this(), "__dh_generator__", encoding_item);
}

extern "C" Item js_dh_generateKeys(Item encoding_item) {
    (void)encoding_item;
    return crypto_throw_dh_not_implemented();
}

extern "C" Item js_dh_computeSecret(Item key_item, Item input_encoding_item, Item output_encoding_item) {
    (void)key_item;
    (void)input_encoding_item;
    (void)output_encoding_item;
    return crypto_throw_dh_not_implemented();
}

static Item crypto_create_dh_object(const uint8_t* prime, int prime_len,
                                    const uint8_t* generator, int generator_len,
                                    const char* constructor_name) {
    Item obj = js_new_object();
    crypto_link_instance_to_constructor(obj, constructor_name);
    js_property_set(obj, make_string_item_crypto("__dh_prime__"),
        crypto_buffer_from_bytes(prime, prime_len));
    js_property_set(obj, make_string_item_crypto("__dh_generator__"),
        crypto_buffer_from_bytes(generator, generator_len));
    js_property_set(obj, make_string_item_crypto("verifyError"),
        (Item){.item = i2it(prime_len <= 1 ? 1 : 0)});
    js_property_set(obj, make_string_item_crypto("getPrime"),
        js_new_function((void*)js_dh_getPrime, 1));
    js_property_set(obj, make_string_item_crypto("getGenerator"),
        js_new_function((void*)js_dh_getGenerator, 1));
    js_property_set(obj, make_string_item_crypto("generateKeys"),
        js_new_function((void*)js_dh_generateKeys, 1));
    js_property_set(obj, make_string_item_crypto("computeSecret"),
        js_new_function((void*)js_dh_computeSecret, 3));
    return obj;
}

extern "C" Item js_crypto_createDiffieHellman(Item size_or_key_item, Item key_encoding_or_generator_item,
                                               Item generator_item, Item generator_encoding_item) {
    uint8_t* prime = NULL;
    int prime_len = 0;
    uint8_t* generator = NULL;
    int generator_len = 0;
    TypeId first_type = get_type_id(size_or_key_item);

    if (first_type == LMD_TYPE_INT || first_type == LMD_TYPE_INT64 || first_type == LMD_TYPE_FLOAT) {
        int bits = 0;
        if (!crypto_item_to_integer(size_or_key_item, "sizeOrKey", &bits)) return ItemNull;
        if (!crypto_generate_dh_prime(bits, &prime, &prime_len)) return ItemNull;
        if (!crypto_parse_dh_generator(key_encoding_or_generator_item, generator_item, &generator, &generator_len)) {
            mem_free(prime);
            return ItemNull;
        }
    } else {
        Item key_encoding = make_js_undefined_crypto();
        Item gen_value = key_encoding_or_generator_item;
        Item gen_encoding = generator_item;
        if (get_type_id(key_encoding_or_generator_item) == LMD_TYPE_STRING) {
            key_encoding = key_encoding_or_generator_item;
            gen_value = generator_item;
            gen_encoding = generator_encoding_item;
        }
        if (!crypto_extract_bytes_with_encoding(size_or_key_item, key_encoding, &prime, &prime_len)) {
            return js_throw_invalid_arg_type("sizeOrKey", "number, string, ArrayBuffer, Buffer, TypedArray, or DataView", size_or_key_item);
        }
        if (!crypto_parse_dh_generator(gen_value, gen_encoding, &generator, &generator_len)) {
            mem_free(prime);
            return ItemNull;
        }
    }

    Item obj = crypto_create_dh_object(prime, prime_len, generator, generator_len, "DiffieHellman");
    mem_free(prime);
    mem_free(generator);
    return obj;
}

extern "C" Item js_crypto_createDiffieHellmanGroup(Item name_item) {
    const CryptoDhGroupDef* group = crypto_dh_group_lookup(name_item);
    if (!group) return crypto_throw_unknown_dh_group();

    uint8_t* prime = NULL;
    int prime_len = 0;
    if (!crypto_hex_const_to_bytes(group->prime_hex, &prime, &prime_len)) return ItemNull;
    uint8_t generator[1] = {2};
    Item obj = crypto_create_dh_object(prime, prime_len, generator, 1, "DiffieHellmanGroup");
    mem_free(prime);
    return obj;
}

extern "C" Item js_crypto_getDiffieHellman(Item name_item) {
    return js_crypto_createDiffieHellmanGroup(name_item);
}

static bool crypto_curve_supported(Item curve_item, char* out, int out_size) {
    if (get_type_id(curve_item) != LMD_TYPE_STRING) return false;
    if (!crypto_normalize_string(curve_item, out, out_size, false)) return false;
    return strcmp(out, "prime256v1") == 0 ||
           strcmp(out, "secp384r1") == 0 ||
           strcmp(out, "secp521r1") == 0;
}

static Item crypto_throw_ecdh_not_implemented(void) {
    return js_throw_error_with_code(JS_ERR_METHOD_NOT_IMPLEMENTED,
        "ECDH key agreement is not supported by this crypto backend yet");
}

extern "C" Item js_ecdh_generateKeys(Item encoding_item, Item format_item) {
    (void)encoding_item;
    (void)format_item;
    return crypto_throw_ecdh_not_implemented();
}

extern "C" Item js_ecdh_computeSecret(Item key_item, Item input_encoding_item, Item output_encoding_item) {
    (void)key_item;
    (void)input_encoding_item;
    (void)output_encoding_item;
    return crypto_throw_ecdh_not_implemented();
}

extern "C" Item js_ecdh_getPublicKey(Item encoding_item, Item format_item) {
    (void)encoding_item;
    (void)format_item;
    return js_throw_error_with_code("ERR_CRYPTO_ECDH_INVALID_PUBLIC_KEY", "Failed to get ECDH public key");
}

extern "C" Item js_ecdh_getPrivateKey(Item encoding_item) {
    (void)encoding_item;
    return js_throw_error_with_code("ERR_CRYPTO_ECDH_INVALID_PRIVATE_KEY", "Failed to get ECDH private key");
}

extern "C" Item js_ecdh_setPrivateKey(Item key_item, Item encoding_item) {
    (void)key_item;
    (void)encoding_item;
    return crypto_throw_ecdh_not_implemented();
}

extern "C" Item js_ecdh_setPublicKey(Item key_item, Item encoding_item) {
    (void)key_item;
    (void)encoding_item;
    return crypto_throw_ecdh_not_implemented();
}

extern "C" Item js_crypto_createECDH(Item curve_item) {
    char curve[32];
    if (get_type_id(curve_item) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("curve", "string", curve_item);
    }
    if (!crypto_curve_supported(curve_item, curve, sizeof(curve))) {
        return js_throw_type_error_code("ERR_CRYPTO_INVALID_CURVE", "Invalid EC curve name");
    }

    Item obj = js_new_object();
    crypto_link_instance_to_constructor(obj, "ECDH");
    js_property_set(obj, make_string_item_crypto("__ecdh_curve__"), make_string_item_crypto(curve));
    js_property_set(obj, make_string_item_crypto("generateKeys"),
        js_new_function((void*)js_ecdh_generateKeys, 2));
    js_property_set(obj, make_string_item_crypto("computeSecret"),
        js_new_function((void*)js_ecdh_computeSecret, 3));
    js_property_set(obj, make_string_item_crypto("getPublicKey"),
        js_new_function((void*)js_ecdh_getPublicKey, 2));
    js_property_set(obj, make_string_item_crypto("getPrivateKey"),
        js_new_function((void*)js_ecdh_getPrivateKey, 1));
    js_property_set(obj, make_string_item_crypto("setPrivateKey"),
        js_new_function((void*)js_ecdh_setPrivateKey, 2));
    js_property_set(obj, make_string_item_crypto("setPublicKey"),
        js_new_function((void*)js_ecdh_setPublicKey, 2));
    return obj;
}

extern "C" Item js_crypto_getCurves(void) {
    Item arr = js_array_new(0);
    js_array_push(arr, make_string_item_crypto("prime256v1"));
    js_array_push(arr, make_string_item_crypto("secp384r1"));
    js_array_push(arr, make_string_item_crypto("secp521r1"));
    return arr;
}

// ============================================================================
// timingSafeEqual(a, b) → boolean
// ============================================================================

extern "C" Item js_crypto_timingSafeEqual(Item a_item, Item b_item) {
    const uint8_t *a_buf, *b_buf;
    int a_len, b_len;
    if (!get_uint8_buffer(a_item, &a_buf, &a_len) || !get_uint8_buffer(b_item, &b_buf, &b_len))
        return (Item){.item = b2it(false)};
    if (a_len != b_len) return (Item){.item = b2it(false)};
    volatile uint8_t diff = 0;
    for (int i = 0; i < a_len; i++) diff |= a_buf[i] ^ b_buf[i];
    return (Item){.item = b2it(diff == 0)};
}

// ============================================================================
// createCipheriv / createDecipheriv — AES encryption/decryption via mbedTLS
// ============================================================================

#include <mbedtls/cipher.h>
#include <mbedtls/gcm.h>
#include <mbedtls/nist_kw.h>

// map Node.js algorithm name to mbedTLS cipher type
static mbedtls_cipher_type_t resolve_cipher_type(const char* alg, int key_len) {
    if (strcmp(alg, "aes-128-ecb") == 0) return MBEDTLS_CIPHER_AES_128_ECB;
    if (strcmp(alg, "aes-192-ecb") == 0) return MBEDTLS_CIPHER_AES_192_ECB;
    if (strcmp(alg, "aes-256-ecb") == 0) return MBEDTLS_CIPHER_AES_256_ECB;
    if (strcmp(alg, "aes-128-cbc") == 0) return MBEDTLS_CIPHER_AES_128_CBC;
    if (strcmp(alg, "aes-192-cbc") == 0) return MBEDTLS_CIPHER_AES_192_CBC;
    if (strcmp(alg, "aes-256-cbc") == 0) return MBEDTLS_CIPHER_AES_256_CBC;
    if (strcmp(alg, "aes-128-ctr") == 0) return MBEDTLS_CIPHER_AES_128_CTR;
    if (strcmp(alg, "aes-192-ctr") == 0) return MBEDTLS_CIPHER_AES_192_CTR;
    if (strcmp(alg, "aes-256-ctr") == 0) return MBEDTLS_CIPHER_AES_256_CTR;
    if (strcmp(alg, "aes-128-gcm") == 0) return MBEDTLS_CIPHER_AES_128_GCM;
    if (strcmp(alg, "aes-192-gcm") == 0) return MBEDTLS_CIPHER_AES_192_GCM;
    if (strcmp(alg, "aes-256-gcm") == 0) return MBEDTLS_CIPHER_AES_256_GCM;
    if (strcmp(alg, "id-aes128-wrap") == 0 && key_len == 16) return MBEDTLS_CIPHER_AES_128_KW;
    if (strcmp(alg, "des-ede3-cbc") == 0 && key_len == 24) return MBEDTLS_CIPHER_DES_EDE3_CBC;
    // auto-detect key size for generic names
    if (strcmp(alg, "aes-cbc") == 0) {
        if (key_len == 16) return MBEDTLS_CIPHER_AES_128_CBC;
        if (key_len == 24) return MBEDTLS_CIPHER_AES_192_CBC;
        if (key_len == 32) return MBEDTLS_CIPHER_AES_256_CBC;
    }
    if (strcmp(alg, "aes-ctr") == 0) {
        if (key_len == 16) return MBEDTLS_CIPHER_AES_128_CTR;
        if (key_len == 24) return MBEDTLS_CIPHER_AES_192_CTR;
        if (key_len == 32) return MBEDTLS_CIPHER_AES_256_CTR;
    }
    if (strcmp(alg, "aes-gcm") == 0) {
        if (key_len == 16) return MBEDTLS_CIPHER_AES_128_GCM;
        if (key_len == 24) return MBEDTLS_CIPHER_AES_192_GCM;
        if (key_len == 32) return MBEDTLS_CIPHER_AES_256_GCM;
    }
    return MBEDTLS_CIPHER_NONE;
}

static bool is_gcm_cipher(const char* alg) {
    return strstr(alg, "gcm") != NULL;
}

static bool is_ecb_cipher(const char* alg) {
    return strstr(alg, "ecb") != NULL;
}

static bool is_kw_cipher(const char* alg) {
    return strcmp(alg, "id-aes128-wrap") == 0;
}

static bool is_known_cipher_name(const char* alg) {
    return strcmp(alg, "aes-128-ecb") == 0 ||
           strcmp(alg, "aes-192-ecb") == 0 ||
           strcmp(alg, "aes-256-ecb") == 0 ||
           strcmp(alg, "aes-128-cbc") == 0 ||
           strcmp(alg, "aes-192-cbc") == 0 ||
           strcmp(alg, "aes-256-cbc") == 0 ||
           strcmp(alg, "aes-128-ctr") == 0 ||
           strcmp(alg, "aes-192-ctr") == 0 ||
           strcmp(alg, "aes-256-ctr") == 0 ||
           strcmp(alg, "aes-128-gcm") == 0 ||
           strcmp(alg, "aes-192-gcm") == 0 ||
           strcmp(alg, "aes-256-gcm") == 0 ||
           strcmp(alg, "aes-cbc") == 0 ||
           strcmp(alg, "aes-ctr") == 0 ||
           strcmp(alg, "aes-gcm") == 0 ||
           strcmp(alg, "id-aes128-wrap") == 0 ||
           strcmp(alg, "des-ede3-cbc") == 0;
}

static Item crypto_throw_unknown_cipher(void) {
    return js_throw_error_with_code("ERR_CRYPTO_UNKNOWN_CIPHER", "Unknown cipher");
}

static Item crypto_throw_invalid_key_length(void) {
    return js_throw_range_error_code("ERR_CRYPTO_INVALID_KEYLEN", "Invalid key length");
}

static Item crypto_throw_invalid_initialization_vector(void) {
    return js_throw_type_error("Invalid initialization vector");
}

static bool crypto_cipher_iv_length_valid(const char* alg, int iv_len) {
    if (is_ecb_cipher(alg)) return iv_len == 0;
    if (is_kw_cipher(alg)) return iv_len == 8;
    if (is_gcm_cipher(alg)) return iv_len > 0;
    if (strcmp(alg, "des-ede3-cbc") == 0) return iv_len == 8;
    return iv_len == 16;
}

static bool crypto_cipher_key_length_valid(const char* alg, int key_len) {
    if (strncmp(alg, "aes-128-", 8) == 0) return key_len == 16;
    if (strncmp(alg, "aes-192-", 8) == 0) return key_len == 24;
    if (strncmp(alg, "aes-256-", 8) == 0) return key_len == 32;
    if (strcmp(alg, "aes-cbc") == 0 || strcmp(alg, "aes-ctr") == 0 || strcmp(alg, "aes-gcm") == 0) {
        return key_len == 16 || key_len == 24 || key_len == 32;
    }
    if (strcmp(alg, "id-aes128-wrap") == 0) return key_len == 16;
    if (strcmp(alg, "des-ede3-cbc") == 0) return key_len == 24;
    return false;
}

struct CipherCtx {
    char alg[32];
    char output_encoding[32];
    mbedtls_cipher_context_t cipher;
    mbedtls_gcm_context gcm;
    bool use_gcm;
    bool use_kw;
    bool encrypting;
    uint8_t* key;
    int key_len;
    uint8_t* iv;
    int iv_len;
    uint8_t* aad;
    int aad_len;
    uint8_t* data;
    int data_len;
    int data_cap;
    uint8_t auth_tag[16];
    bool has_auth_tag;
    bool finalized;
    bool has_output_encoding;
};

static void cipher_ctx_append_data(CipherCtx* ctx, const uint8_t* buf, int len) {
    int need = ctx->data_len + len;
    if (need > ctx->data_cap) {
        int cap = ctx->data_cap == 0 ? 1024 : ctx->data_cap;
        while (cap < need) cap *= 2;
        ctx->data = (uint8_t*)mem_realloc(ctx->data, (size_t)cap, MEM_CAT_JS_RUNTIME);
        ctx->data_cap = cap;
    }
    memcpy(ctx->data + ctx->data_len, buf, (size_t)len);
    ctx->data_len += len;
}

static void cipher_ctx_free(CipherCtx* ctx) {
    if (!ctx) return;
    if (ctx->use_gcm) mbedtls_gcm_free(&ctx->gcm);
    else if (!ctx->use_kw) mbedtls_cipher_free(&ctx->cipher);
    if (ctx->key) mem_free(ctx->key);
    if (ctx->iv) mem_free(ctx->iv);
    if (ctx->aad) mem_free(ctx->aad);
    if (ctx->data) mem_free(ctx->data);
    mem_free(ctx);
}

static bool crypto_is_known_output_encoding(const char* enc) {
    if (!enc || enc[0] == '\0') return true;
    return strcmp(enc, "buffer") == 0 ||
           strcmp(enc, "hex") == 0 ||
           strcmp(enc, "base64") == 0 ||
           strcmp(enc, "base64url") == 0 ||
           strcmp(enc, "latin1") == 0 ||
           strcmp(enc, "binary") == 0 ||
           strcmp(enc, "ucs2") == 0 ||
           strcmp(enc, "ucs-2") == 0 ||
           strcmp(enc, "utf16le") == 0 ||
           strcmp(enc, "utf-16le") == 0 ||
           strcmp(enc, "utf8") == 0 ||
           strcmp(enc, "utf-8") == 0;
}

static Item crypto_throw_unknown_encoding(const char* enc) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Unknown encoding: %s", enc ? enc : "");
    return js_throw_type_error_code(JS_ERR_UNKNOWN_ENCODING, msg);
}

static bool cipher_normalize_output_encoding(Item encoding_item, char* out, int out_size, bool* has_encoding) {
    if (!crypto_normalize_encoding(encoding_item, out, out_size, has_encoding)) return false;
    if (has_encoding && *has_encoding && !crypto_is_known_output_encoding(out)) {
        crypto_throw_unknown_encoding(out);
        return false;
    }
    return true;
}

static bool cipher_accept_output_encoding(CipherCtx* ctx, const char* enc, bool has_encoding) {
    if (!ctx || !has_encoding || !enc || enc[0] == '\0') return true;
    if (!ctx->has_output_encoding) {
        int len = (int)strlen(enc);
        if (len >= (int)sizeof(ctx->output_encoding)) len = (int)sizeof(ctx->output_encoding) - 1;
        memcpy(ctx->output_encoding, enc, (size_t)len);
        ctx->output_encoding[len] = '\0';
        ctx->has_output_encoding = true;
        return true;
    }
    if (strcmp(ctx->output_encoding, enc) != 0) {
        js_throw_type_error("Cannot change encoding");
        return false;
    }
    return true;
}

static Item cipher_output_from_bytes(const uint8_t* bytes, int len, const char* enc, bool has_encoding) {
    if (has_encoding && enc && strcmp(enc, "base64url") == 0) {
        size_t out_len = base64_encoded_len((size_t)len, BASE64_URL);
        char* out = (char*)mem_alloc(out_len + 1, MEM_CAT_JS_RUNTIME);
        base64_encode(bytes ? bytes : crypto_empty_bytes, (size_t)len, out, BASE64_URL);
        Item result = make_string_item_crypto(out);
        mem_free(out);
        return result;
    }
    return crypto_digest_output_for_encoding(bytes, len, enc, has_encoding);
}

// extract key/iv from string or Uint8Array
static bool extract_bytes(Item item, uint8_t** out, int* out_len) {
    if (!out || !out_len) return false;
    *out = NULL;
    *out_len = 0;

    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* s = it2s(item);
        size_t alloc_len = s->len > 0 ? s->len : 1;
        *out = (uint8_t*)mem_alloc(alloc_len, MEM_CAT_JS_RUNTIME);
        if (s->len > 0) memcpy(*out, s->chars, s->len);
        *out_len = (int)s->len;
        return true;
    } else if (js_is_typed_array(item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(item, &buf, &len)) {
            size_t alloc_len = len > 0 ? (size_t)len : 1;
            *out = (uint8_t*)mem_alloc(alloc_len, MEM_CAT_JS_RUNTIME);
            if (len > 0) memcpy(*out, buf, (size_t)len);
            *out_len = len;
            return true;
        }
    } else if (js_is_dataview(item)) {
        JsDataView* dv = js_get_dataview_ptr(item);
        if (!dv || !dv->buffer || dv->buffer->detached) return false;
        int len = dv->length_tracking ? (dv->buffer->byte_length - dv->byte_offset) : dv->byte_length;
        if (len < 0) len = 0;
        size_t alloc_len = len > 0 ? (size_t)len : 1;
        *out = (uint8_t*)mem_alloc(alloc_len, MEM_CAT_JS_RUNTIME);
        if (len > 0 && dv->buffer->data) {
            memcpy(*out, (uint8_t*)dv->buffer->data + dv->byte_offset, (size_t)len);
        }
        *out_len = len;
        return true;
    } else if (js_is_arraybuffer(item)) {
        JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(item);
        if (!ab || ab->detached) return false;
        int len = ab->byte_length;
        size_t alloc_len = len > 0 ? (size_t)len : 1;
        *out = (uint8_t*)mem_alloc(alloc_len, MEM_CAT_JS_RUNTIME);
        if (len > 0 && ab->data) memcpy(*out, ab->data, (size_t)len);
        *out_len = len;
        return true;
    } else if (get_type_id(item) == LMD_TYPE_MAP) {
        Item key_bytes = js_property_get(item, make_string_item_crypto("__crypto_secret_key__"));
        const uint8_t* buf; int len;
        if (get_uint8_buffer(key_bytes, &buf, &len)) {
            size_t alloc_len = len > 0 ? (size_t)len : 1;
            *out = (uint8_t*)mem_alloc(alloc_len, MEM_CAT_JS_RUNTIME);
            if (len > 0) memcpy(*out, buf, (size_t)len);
            *out_len = len;
            return true;
        }
    }
    return false;
}

static Item crypto_arraybuffer_from_bytes(const uint8_t* bytes, int len) {
    if (len < 0) len = 0;
    Item result = js_arraybuffer_new(len);
    JsArrayBuffer* ab = js_get_arraybuffer_ptr_item(result);
    if (ab && ab->data && bytes && len > 0) {
        memcpy(ab->data, bytes, (size_t)len);
    }
    return result;
}

extern "C" Item js_crypto_secretKeyExport(Item options_item) {
    (void)options_item;
    Item self = js_get_current_this();
    Item key_bytes = js_property_get(self, make_string_item_crypto("__crypto_secret_key__"));
    const uint8_t* buf = NULL;
    int len = 0;
    if (!get_uint8_buffer(key_bytes, &buf, &len)) return ItemNull;

    Item result = js_typed_array_new(JS_TYPED_UINT8, len);
    JsTypedArray* ta = js_get_typed_array_ptr(result.map);
    if (ta && ta->data && buf && len > 0) memcpy(ta->data, buf, (size_t)len);
    return result;
}

static Item crypto_secret_key_object_from_bytes(const uint8_t* key, int key_len) {
    if (key_len < 0) key_len = 0;
    Item bytes = js_typed_array_new(JS_TYPED_UINT8, key_len);
    JsTypedArray* ta = js_get_typed_array_ptr(bytes.map);
    if (ta && ta->data && key && key_len > 0) memcpy(ta->data, key, (size_t)key_len);

    Item obj = js_new_object();
    js_property_set(obj, make_string_item_crypto("__crypto_secret_key__"), bytes);
    js_property_set(obj, make_string_item_crypto("type"), make_string_item_crypto("secret"));
    js_property_set(obj, make_string_item_crypto("symmetricKeySize"), (Item){.item = i2it(key_len)});
    js_property_set(obj, make_string_item_crypto("export"),
                    js_new_function((void*)js_crypto_secretKeyExport, 1));
    return obj;
}

extern "C" Item js_crypto_createSecretKey(Item key_item, Item encoding_item) {
    (void)encoding_item;
    uint8_t* key = NULL;
    int key_len = 0;
    if (!extract_bytes(key_item, &key, &key_len)) {
        return js_throw_invalid_arg_type("key", "string, ArrayBuffer, Buffer, TypedArray, or DataView", key_item);
    }

    Item obj = crypto_secret_key_object_from_bytes(key, key_len);
    mem_free(key);
    return obj;
}

// ============================================================================
// generateKeySync/generateKey — symmetric secret key generation
// ============================================================================

static bool crypto_string_equals(Item item, const char* expected) {
    if (get_type_id(item) != LMD_TYPE_STRING || !expected) return false;
    String* s = it2s(item);
    size_t len = strlen(expected);
    return s && s->len == len && memcmp(s->chars, expected, len) == 0;
}

static Item crypto_throw_invalid_property_type(const char* prop, const char* expected) {
    char msg[256];
    snprintf(msg, sizeof(msg), "The \"%s\" property must be of type %s.", prop, expected);
    return js_throw_type_error_code("ERR_INVALID_ARG_TYPE", msg);
}

static Item crypto_throw_invalid_property_value(const char* prop, const char* expected) {
    char msg[256];
    snprintf(msg, sizeof(msg), "The property '%s' must be one of: %s", prop, expected);
    return js_throw_type_error_code("ERR_INVALID_ARG_VALUE", msg);
}

static bool crypto_keygen_length(Item options_item, int* out_bits) {
    if (!out_bits) return false;
    *out_bits = 0;
    if (get_type_id(options_item) != LMD_TYPE_MAP) {
        js_throw_invalid_arg_type("options", "Object", options_item);
        return false;
    }

    Item length_item = js_property_get(options_item, make_string_item_crypto("length"));
    TypeId length_type = get_type_id(length_item);
    if (length_type != LMD_TYPE_INT && length_type != LMD_TYPE_FLOAT && length_type != LMD_TYPE_INT64) {
        crypto_throw_invalid_property_type("options.length", "number");
        return false;
    }

    double value = 0.0;
    if (length_type == LMD_TYPE_INT) value = (double)it2i(length_item);
    else if (length_type == LMD_TYPE_FLOAT) value = it2d(length_item);
    else value = (double)it2l(length_item);

    if (value != value || value < 0.0 || value > 2147483647.0) {
        js_throw_out_of_range("options.length", ">= 0 && <= 2147483647", length_item);
        return false;
    }
    *out_bits = (int)value;
    return true;
}

extern "C" Item js_crypto_generateKeySync(Item type_item, Item options_item) {
    if (get_type_id(type_item) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("type", "string", type_item);
    }

    bool is_aes = crypto_string_equals(type_item, "aes");
    bool is_hmac = crypto_string_equals(type_item, "hmac");
    if (!is_aes && !is_hmac) {
        return js_throw_type_error_code("ERR_INVALID_ARG_VALUE",
            "The argument 'type' must be a supported key type");
    }

    int bits = 0;
    if (!crypto_keygen_length(options_item, &bits)) return ItemNull;

    int bytes = 0;
    if (is_aes) {
        if (bits != 128 && bits != 192 && bits != 256) {
            return crypto_throw_invalid_property_value("options.length", "128, 192, 256");
        }
        bytes = bits / 8;
    } else {
        if (bits < 8 || bits > 2147483647) {
            return js_throw_out_of_range("options.length", ">= 8 && <= 2147483647", (Item){.item = i2it(bits)});
        }
        bytes = bits / 8;
    }

    uint8_t* key = (uint8_t*)mem_alloc((size_t)(bytes > 0 ? bytes : 1), MEM_CAT_JS_RUNTIME);
    if (bytes > 0 && !crypto_random_bytes(key, (size_t)bytes)) {
        mem_free(key);
        log_error("crypto: generateKeySync: entropy source failed");
        return ItemNull;
    }
    Item result = crypto_secret_key_object_from_bytes(key, bytes);
    mem_free(key);
    return result;
}

static Item js_crypto_generateKey_emit(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined_crypto();
    Item callback = env[0];
    Item result = env[1];
    Item args[2] = { ItemNull, result };
    js_call_function(callback, make_js_undefined_crypto(), args, 2);
    return make_js_undefined_crypto();
}

extern "C" Item js_crypto_generateKey(Item type_item, Item options_item, Item callback_item) {
    Item result = js_crypto_generateKeySync(type_item, options_item);
    if (js_check_exception() || result.item == ITEM_NULL) return ItemNull;

    if (get_type_id(callback_item) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "Function", callback_item);
    }

    Item* env = js_alloc_env(2);
    env[0] = callback_item;
    env[1] = result;
    Item fn = js_new_closure((void*)js_crypto_generateKey_emit, 0, env, 2);
    js_next_tick_enqueue(fn);
    return make_js_undefined_crypto();
}

extern "C" Item js_cipher_update(Item data_item, Item input_encoding_item, Item output_encoding_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__cipher_ctx__"));
    if (ctx_item.item == 0) return ItemNull;
    CipherCtx* ctx = (CipherCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx || ctx->finalized) return ItemNull;

    char in_enc[32];
    bool has_in_enc = false;
    if (!crypto_normalize_encoding(input_encoding_item, in_enc, sizeof(in_enc), &has_in_enc)) return ItemNull;

    char out_enc[32];
    bool has_out_enc = false;
    if (!cipher_normalize_output_encoding(output_encoding_item, out_enc, sizeof(out_enc), &has_out_enc)) return ItemNull;
    if (!cipher_accept_output_encoding(ctx, out_enc, has_out_enc)) return ItemNull;

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        uint8_t* bytes = NULL;
        int len = 0;
        if (!crypto_string_bytes_for_encoding(s, in_enc, has_in_enc, &bytes, &len)) {
            return js_throw_invalid_arg_value("encoding", "is invalid", input_encoding_item);
        }
        cipher_ctx_append_data(ctx, bytes, len);
        mem_free(bytes);
    } else if (js_is_typed_array(data_item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(data_item, &buf, &len))
            cipher_ctx_append_data(ctx, buf, len);
    }

    // for non-GCM streaming, process complete blocks now
    if (!ctx->use_gcm && !ctx->use_kw && ctx->data_len > 0) {
        size_t out_len = (size_t)ctx->data_len + 16;
        uint8_t* out_buf = (uint8_t*)mem_alloc(out_len, MEM_CAT_JS_RUNTIME);
        size_t olen = 0;
        int ret = mbedtls_cipher_update(&ctx->cipher, ctx->data, (size_t)ctx->data_len, out_buf, &olen);
        if (ret != 0) {
            log_error("crypto: cipher update failed: %d", ret);
            mem_free(out_buf);
            return ItemNull;
        }
        ctx->data_len = 0; // consumed
        if (olen > 0) {
            Item result = cipher_output_from_bytes(out_buf, (int)olen, out_enc, has_out_enc);
            mem_free(out_buf);
            return result;
        }
        mem_free(out_buf);
        return cipher_output_from_bytes(crypto_empty_bytes, 0, out_enc, has_out_enc);
    }
    return cipher_output_from_bytes(crypto_empty_bytes, 0, out_enc, has_out_enc);
}

extern "C" Item js_cipher_final(Item output_encoding_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__cipher_ctx__"));
    if (ctx_item.item == 0) return ItemNull;
    CipherCtx* ctx = (CipherCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx || ctx->finalized) return ItemNull;
    char out_enc[32];
    bool has_out_enc = false;
    if (!cipher_normalize_output_encoding(output_encoding_item, out_enc, sizeof(out_enc), &has_out_enc)) return ItemNull;
    if (!cipher_accept_output_encoding(ctx, out_enc, has_out_enc)) return ItemNull;
    log_debug("cipher_final: ctx=%p finalized=%d", ctx, ctx ? ctx->finalized : -1);
    if (!ctx || ctx->finalized) return ItemNull;
    ctx->finalized = true;

    if (ctx->use_gcm) {
        // GCM: do all at once
        size_t out_len = (size_t)ctx->data_len + 16;
        uint8_t* out_buf = (uint8_t*)mem_alloc(out_len, MEM_CAT_JS_RUNTIME);

        if (ctx->encrypting) {
            int ret = mbedtls_gcm_crypt_and_tag(&ctx->gcm, MBEDTLS_GCM_ENCRYPT,
                (size_t)ctx->data_len, ctx->iv, (size_t)ctx->iv_len,
                ctx->aad ? ctx->aad : (const uint8_t*)"", ctx->aad ? (size_t)ctx->aad_len : 0,
                ctx->data ? ctx->data : (const uint8_t*)"", out_buf,
                16, ctx->auth_tag);
            if (ret != 0) {
                log_error("crypto: GCM encrypt failed: %d", ret);
                mem_free(out_buf);
                cipher_ctx_free(ctx);
                js_property_set(self, make_string_item_crypto("__cipher_ctx__"), ItemNull);
                return ItemNull;
            }
            ctx->has_auth_tag = true;
            Item result = cipher_output_from_bytes(out_buf, ctx->data_len, out_enc, has_out_enc);
            mem_free(out_buf);

            // store auth tag on the object
            Item tag = js_typed_array_new(JS_TYPED_UINT8, 16);
            JsTypedArray* tag_ta = (JsTypedArray*)tag.map->data;
            if (tag_ta && tag_ta->data) memcpy(tag_ta->data, ctx->auth_tag, 16);
            js_property_set(self, make_string_item_crypto("__auth_tag__"), tag);

            cipher_ctx_free(ctx);
            js_property_set(self, make_string_item_crypto("__cipher_ctx__"), ItemNull);
            return result;
        } else {
            // decrypt
            int ret = mbedtls_gcm_auth_decrypt(&ctx->gcm,
                (size_t)ctx->data_len, ctx->iv, (size_t)ctx->iv_len,
                ctx->aad ? ctx->aad : (const uint8_t*)"", ctx->aad ? (size_t)ctx->aad_len : 0,
                ctx->has_auth_tag ? ctx->auth_tag : (const uint8_t*)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16,
                ctx->data ? ctx->data : (const uint8_t*)"", out_buf);
            if (ret != 0) {
                log_error("crypto: GCM decrypt failed: %d", ret);
                mem_free(out_buf);
                cipher_ctx_free(ctx);
                js_property_set(self, make_string_item_crypto("__cipher_ctx__"), ItemNull);
                return ItemNull;
            }
            Item result = cipher_output_from_bytes(out_buf, ctx->data_len, out_enc, has_out_enc);
            mem_free(out_buf);
            cipher_ctx_free(ctx);
            js_property_set(self, make_string_item_crypto("__cipher_ctx__"), ItemNull);
            return result;
        }
    } else if (ctx->use_kw) {
        mbedtls_nist_kw_context kw;
        mbedtls_nist_kw_init(&kw);
        int ret = mbedtls_nist_kw_setkey(&kw, MBEDTLS_CIPHER_ID_AES,
            ctx->key, (unsigned int)(ctx->key_len * 8), ctx->encrypting ? 1 : 0);
        if (ret != 0) {
            log_error("crypto: AES-KW setkey failed: %d", ret);
            mbedtls_nist_kw_free(&kw);
            cipher_ctx_free(ctx);
            js_property_set(self, make_string_item_crypto("__cipher_ctx__"), ItemNull);
            return ItemNull;
        }

        size_t out_cap = (size_t)ctx->data_len + 8;
        uint8_t* out_buf = (uint8_t*)mem_alloc(out_cap > 0 ? out_cap : 1, MEM_CAT_JS_RUNTIME);
        size_t out_len = 0;
        if (ctx->encrypting) {
            ret = mbedtls_nist_kw_wrap(&kw, MBEDTLS_KW_MODE_KW,
                ctx->data ? ctx->data : crypto_empty_bytes, (size_t)ctx->data_len,
                out_buf, &out_len, out_cap);
        } else {
            ret = mbedtls_nist_kw_unwrap(&kw, MBEDTLS_KW_MODE_KW,
                ctx->data ? ctx->data : crypto_empty_bytes, (size_t)ctx->data_len,
                out_buf, &out_len, out_cap);
        }
        bool encrypting = ctx->encrypting;
        mbedtls_nist_kw_free(&kw);
        cipher_ctx_free(ctx);
        js_property_set(self, make_string_item_crypto("__cipher_ctx__"), ItemNull);
        if (ret != 0) {
            log_error("crypto: AES-KW %s failed: %d", encrypting ? "wrap" : "unwrap", ret);
            mem_free(out_buf);
            return ItemNull;
        }
        Item result = cipher_output_from_bytes(out_buf, (int)out_len, out_enc, has_out_enc);
        mem_free(out_buf);
        return result;
    } else {
        // CBC/CTR: finish with padding
        uint8_t finish_buf[32];
        size_t olen = 0;
        int ret = mbedtls_cipher_finish(&ctx->cipher, finish_buf, &olen);
        cipher_ctx_free(ctx);
        js_property_set(self, make_string_item_crypto("__cipher_ctx__"), ItemNull);
        if (ret != 0) {
            log_error("crypto: cipher finish failed: %d", ret);
            return ItemNull;
        }
        if (olen > 0) {
            Item result = cipher_output_from_bytes(finish_buf, (int)olen, out_enc, has_out_enc);
            return result;
        }
        return cipher_output_from_bytes(crypto_empty_bytes, 0, out_enc, has_out_enc);
    }
}

static int crypto_item_byte_length(Item item) {
    const uint8_t* buf = NULL;
    int len = 0;
    if (get_uint8_buffer(item, &buf, &len)) return len;
    return 0;
}

static Item crypto_concat_buffer_items(Item first, Item second) {
    const uint8_t* first_buf = NULL;
    const uint8_t* second_buf = NULL;
    int first_len = 0;
    int second_len = 0;
    get_uint8_buffer(first, &first_buf, &first_len);
    get_uint8_buffer(second, &second_buf, &second_len);

    int total = first_len + second_len;
    Item result = crypto_buffer_from_bytes(NULL, total);
    JsTypedArray* ta = js_get_typed_array_ptr(result.map);
    if (ta && ta->data) {
        if (first_buf && first_len > 0) memcpy(ta->data, first_buf, (size_t)first_len);
        if (second_buf && second_len > 0) {
            memcpy((uint8_t*)ta->data + first_len, second_buf, (size_t)second_len);
        }
    }
    return result;
}

extern "C" Item js_cipher_end(Item data_item) {
    Item self = js_get_current_this();
    Item chunk = crypto_buffer_from_bytes(NULL, 0);
    if (!crypto_item_is_undefined(data_item) && data_item.item != ITEM_NULL) {
        chunk = js_cipher_update(data_item, make_js_undefined_crypto(), make_string_item_crypto("buffer"));
        if (js_check_exception()) return ItemNull;
    }
    Item tail = js_cipher_final(make_string_item_crypto("buffer"));
    if (js_check_exception()) return ItemNull;
    Item combined = crypto_concat_buffer_items(chunk, tail);
    int len = crypto_item_byte_length(combined);
    js_property_set(self, make_string_item_crypto("__cipher_read__"), combined);
    js_property_set(self, make_string_item_crypto("readableLength"), (Item){.item = i2it(len)});
    return self;
}

extern "C" Item js_cipher_read(Item size_item) {
    (void)size_item;
    Item self = js_get_current_this();
    Item data = js_property_get(self, make_string_item_crypto("__cipher_read__"));
    if (data.item == 0 || data.item == ITEM_NULL) return ItemNull;
    js_property_set(self, make_string_item_crypto("__cipher_read__"), ItemNull);
    js_property_set(self, make_string_item_crypto("readableLength"), (Item){.item = i2it(0)});
    return data;
}

extern "C" Item js_cipher_getAuthTag(void) {
    Item self = js_get_current_this();
    return js_property_get(self, make_string_item_crypto("__auth_tag__"));
}

extern "C" Item js_cipher_setAuthTag(Item tag_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__cipher_ctx__"));
    if (ctx_item.item == 0) return self;
    CipherCtx* ctx = (CipherCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx) return self;

    const uint8_t* buf; int len;
    if (get_uint8_buffer(tag_item, &buf, &len) && len == 16) {
        memcpy(ctx->auth_tag, buf, 16);
        ctx->has_auth_tag = true;
    }
    return self;
}

extern "C" Item js_cipher_setAAD(Item aad_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__cipher_ctx__"));
    if (ctx_item.item == 0) return self;
    CipherCtx* ctx = (CipherCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx) return self;

    if (ctx->aad) { mem_free(ctx->aad); ctx->aad = NULL; ctx->aad_len = 0; }
    if (get_type_id(aad_item) == LMD_TYPE_STRING) {
        String* s = it2s(aad_item);
        ctx->aad = (uint8_t*)mem_alloc(s->len, MEM_CAT_JS_RUNTIME);
        memcpy(ctx->aad, s->chars, s->len);
        ctx->aad_len = (int)s->len;
    } else if (js_is_typed_array(aad_item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(aad_item, &buf, &len)) {
            ctx->aad = (uint8_t*)mem_alloc((size_t)len, MEM_CAT_JS_RUNTIME);
            memcpy(ctx->aad, buf, (size_t)len);
            ctx->aad_len = len;
        }
    }
    return self;
}

static Item create_cipher_object(const char* alg, bool encrypting,
                                  uint8_t* key, int key_len,
                                  uint8_t* iv, int iv_len) {
    mbedtls_cipher_type_t ct = resolve_cipher_type(alg, key_len);
    if (ct == MBEDTLS_CIPHER_NONE) {
        log_error("crypto: unsupported cipher algorithm: %s", alg);
        mem_free(key); mem_free(iv);
        return ItemNull;
    }

    CipherCtx* ctx = (CipherCtx*)mem_calloc(1, sizeof(CipherCtx), MEM_CAT_JS_RUNTIME);
    int alen = (int)strlen(alg);
    if (alen > 31) alen = 31;
    memcpy(ctx->alg, alg, (size_t)alen);
    ctx->alg[alen] = '\0';
    ctx->encrypting = encrypting;
    ctx->key = key;
    ctx->key_len = key_len;
    ctx->iv = iv;
    ctx->iv_len = iv_len;

    if (is_gcm_cipher(alg)) {
        ctx->use_gcm = true;
        mbedtls_gcm_init(&ctx->gcm);
        int ret = mbedtls_gcm_setkey(&ctx->gcm, MBEDTLS_CIPHER_ID_AES, key, (unsigned int)(key_len * 8));
        if (ret != 0) {
            log_error("crypto: GCM setkey failed: %d", ret);
            cipher_ctx_free(ctx);
            return ItemNull;
        }
    } else if (is_kw_cipher(alg)) {
        ctx->use_kw = true;
        if (key_len != 16 || iv_len != 8) {
            log_error("crypto: invalid AES-KW key/iv length: key=%d iv=%d", key_len, iv_len);
            cipher_ctx_free(ctx);
            return ItemNull;
        }
    } else {
        ctx->use_gcm = false;
        mbedtls_cipher_init(&ctx->cipher);
        const mbedtls_cipher_info_t* ci = mbedtls_cipher_info_from_type(ct);
        if (!ci) {
            log_error("crypto: cipher info not found for type %d", ct);
            cipher_ctx_free(ctx);
            return ItemNull;
        }
        int ret = mbedtls_cipher_setup(&ctx->cipher, ci);
        if (ret != 0) { cipher_ctx_free(ctx); return ItemNull; }
        // mbedTLS 3.x requires explicit padding mode for CBC
        mbedtls_cipher_set_padding_mode(&ctx->cipher, MBEDTLS_PADDING_PKCS7);
        ret = mbedtls_cipher_setkey(&ctx->cipher, key, (int)(key_len * 8),
                                     encrypting ? MBEDTLS_ENCRYPT : MBEDTLS_DECRYPT);
        if (ret != 0) { cipher_ctx_free(ctx); return ItemNull; }
        if (!is_ecb_cipher(alg)) {
            ret = mbedtls_cipher_set_iv(&ctx->cipher, iv, (size_t)iv_len);
            if (ret != 0) { cipher_ctx_free(ctx); return ItemNull; }
        }
        ret = mbedtls_cipher_reset(&ctx->cipher);
        if (ret != 0) { cipher_ctx_free(ctx); return ItemNull; }
    }

    Item obj = js_new_object();
    crypto_link_instance_to_constructor(obj, encrypting ? "Cipheriv" : "Decipheriv");
    js_property_set(obj, make_string_item_crypto("__cipher_ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    js_property_set(obj, make_string_item_crypto("update"),
                    js_new_function((void*)js_cipher_update, 3));
    js_property_set(obj, make_string_item_crypto("final"),
                    js_new_function((void*)js_cipher_final, 1));
    js_property_set(obj, make_string_item_crypto("end"),
                    js_new_function((void*)js_cipher_end, 1));
    js_property_set(obj, make_string_item_crypto("read"),
                    js_new_function((void*)js_cipher_read, 1));
    js_property_set(obj, make_string_item_crypto("readableLength"),
                    (Item){.item = i2it(0)});
    js_property_set(obj, make_string_item_crypto("getAuthTag"),
                    js_new_function((void*)js_cipher_getAuthTag, 0));
    js_property_set(obj, make_string_item_crypto("setAuthTag"),
                    js_new_function((void*)js_cipher_setAuthTag, 1));
    js_property_set(obj, make_string_item_crypto("setAAD"),
                    js_new_function((void*)js_cipher_setAAD, 1));
    return obj;
}

static Item js_crypto_create_cipheriv_common(Item alg_item, Item key_item, Item iv_item, bool encrypting) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("cipher", "string", alg_item);
    }
    String* alg = it2s(alg_item);
    char alg_buf[32];
    int alen = (int)alg->len < 31 ? (int)alg->len : 31;
    memcpy(alg_buf, alg->chars, (size_t)alen);
    alg_buf[alen] = '\0';

    uint8_t* key = NULL; int key_len = 0;
    uint8_t* iv = NULL; int iv_len = 0;
    if (!extract_bytes(key_item, &key, &key_len)) {
        return js_throw_invalid_arg_type("key", "string, ArrayBuffer, Buffer, TypedArray, DataView, or KeyObject", key_item);
    }

    if (!is_known_cipher_name(alg_buf)) {
        mem_free(key);
        return crypto_throw_unknown_cipher();
    }
    if (!crypto_cipher_key_length_valid(alg_buf, key_len)) {
        mem_free(key);
        return crypto_throw_invalid_key_length();
    }

    mbedtls_cipher_type_t ct = resolve_cipher_type(alg_buf, key_len);
    if (ct == MBEDTLS_CIPHER_NONE) {
        mem_free(key);
        return crypto_throw_unknown_cipher();
    }

    if (is_ecb_cipher(alg_buf) && get_type_id(iv_item) == LMD_TYPE_NULL) {
        iv = NULL;
        iv_len = 0;
    } else {
        if (!extract_bytes(iv_item, &iv, &iv_len)) {
            mem_free(key);
            if (get_type_id(iv_item) == LMD_TYPE_NULL) return crypto_throw_invalid_initialization_vector();
            return js_throw_invalid_arg_type("iv", "string, ArrayBuffer, Buffer, TypedArray, or DataView", iv_item);
        }
    }

    if (!crypto_cipher_iv_length_valid(alg_buf, iv_len)) {
        mem_free(key);
        mem_free(iv);
        return crypto_throw_invalid_initialization_vector();
    }

    return create_cipher_object(alg_buf, encrypting, key, key_len, iv, iv_len);
}

extern "C" Item js_crypto_createCipheriv(Item alg_item, Item key_item, Item iv_item) {
    return js_crypto_create_cipheriv_common(alg_item, key_item, iv_item, true);
}

extern "C" Item js_crypto_createDecipheriv(Item alg_item, Item key_item, Item iv_item) {
    return js_crypto_create_cipheriv_common(alg_item, key_item, iv_item, false);
}

// ============================================================================
// pbkdf2Sync(password, salt, iterations, keylen, digest) → Buffer
// pbkdf2(password, salt, iterations, keylen, digest, callback) → void
// ============================================================================

static Item crypto_pbkdf2_invalid_arg(const char* name, Item value) {
    return js_throw_invalid_arg_type(name, "string, ArrayBuffer, Buffer, TypedArray, or DataView", value);
}

static bool crypto_pbkdf2_positive_int(Item value_item, const char* name, int* out_value) {
    if (!out_value) return false;
    TypeId type = get_type_id(value_item);
    if (type != LMD_TYPE_INT && type != LMD_TYPE_FLOAT && type != LMD_TYPE_INT64) {
        js_throw_invalid_arg_type(name, "number", value_item);
        return false;
    }

    double value = 0.0;
    if (type == LMD_TYPE_INT) value = (double)it2i(value_item);
    else if (type == LMD_TYPE_FLOAT) value = it2d(value_item);
    else value = (double)it2l(value_item);

    if (value != value || value < 1.0 || value > 2147483647.0 || value != (double)(int)value) {
        char received[64];
        if (value != value) {
            snprintf(received, sizeof(received), "NaN");
        } else if (value == 1.0 / 0.0) {
            snprintf(received, sizeof(received), "Infinity");
        } else if (value == -1.0 / 0.0) {
            snprintf(received, sizeof(received), "-Infinity");
        } else if (type == LMD_TYPE_INT) {
            snprintf(received, sizeof(received), "%lld", (long long)it2i(value_item));
        } else if (type == LMD_TYPE_INT64) {
            snprintf(received, sizeof(received), "%lld", (long long)it2l(value_item));
        } else {
            snprintf(received, sizeof(received), "%g", value);
        }
        char msg[160];
        snprintf(msg, sizeof(msg),
            "The value of \"%s\" is out of range. It must be an integer. Received %s",
            name, received);
        js_throw_range_error_code(JS_ERR_OUT_OF_RANGE, msg);
        return false;
    }

    *out_value = (int)value;
    return true;
}

static bool crypto_pbkdf2_digest_name(Item digest_item, char* out, int out_cap) {
    if (!out || out_cap <= 0) return false;
    out[0] = '\0';
    if (get_type_id(digest_item) != LMD_TYPE_STRING) {
        js_throw_invalid_arg_type("digest", "string", digest_item);
        return false;
    }
    String* digest = it2s(digest_item);
    int pos = 0;
    for (int i = 0; digest && i < (int)digest->len && pos < out_cap - 1; i++) {
        char c = digest->chars[i];
        if (c == '-') continue;
        out[pos++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    out[pos] = '\0';
    return true;
}

static Item crypto_pbkdf2_invalid_digest(const char* digest) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Invalid digest: %s", digest ? digest : "");
    return js_throw_type_error_code(JS_ERR_CRYPTO_INVALID_DIGEST, msg);
}

extern "C" Item js_crypto_pbkdf2Sync(Item pass_item, Item salt_item, Item iter_item,
                                      Item keylen_item, Item digest_item) {
    uint8_t* pass = NULL; int pass_len = 0;
    uint8_t* salt = NULL; int salt_len = 0;

    if (!extract_bytes(pass_item, &pass, &pass_len)) return crypto_pbkdf2_invalid_arg("password", pass_item);
    if (!extract_bytes(salt_item, &salt, &salt_len)) {
        mem_free(pass);
        return crypto_pbkdf2_invalid_arg("salt", salt_item);
    }

    int iterations = 0;
    int keylen = 0;
    if (!crypto_pbkdf2_positive_int(iter_item, "iterations", &iterations) ||
        !crypto_pbkdf2_positive_int(keylen_item, "keylen", &keylen)) {
        mem_free(pass); mem_free(salt);
        return ItemNull;
    }

    char digest_buf[32];
    if (!crypto_pbkdf2_digest_name(digest_item, digest_buf, (int)sizeof(digest_buf))) {
        mem_free(pass); mem_free(salt);
        return ItemNull;
    }

    int bits = crypto_digest_bits_for_name(digest_buf, true, true);
    if (bits == 0) {
        mem_free(pass); mem_free(salt);
        return crypto_pbkdf2_invalid_digest(digest_buf);
    }

    uint8_t* output = (uint8_t*)mem_alloc((size_t)keylen, MEM_CAT_JS_RUNTIME);
    bool ok = digest_pbkdf2_hmac_bits(bits, pass, (size_t)pass_len,
                                      salt, (size_t)salt_len,
                                      (unsigned int)iterations,
                                      output, (size_t)keylen);
    mem_free(pass);
    mem_free(salt);

    if (!ok) {
        log_error("crypto: pbkdf2Sync failed");
        mem_free(output);
        return ItemNull;
    }

    Item result = crypto_buffer_from_bytes(output, keylen);
    mem_free(output);
    return result;
}

// async variant calls callback with (err, derivedKey)
extern "C" Item js_crypto_pbkdf2(Item pass_item, Item salt_item, Item iter_item,
                                  Item keylen_item, Item digest_item, Item callback_item) {
    if (get_type_id(digest_item) == LMD_TYPE_FUNC && crypto_item_is_undefined(callback_item)) {
        return js_throw_invalid_arg_type("digest", "string", make_js_undefined_crypto());
    }
    if (get_type_id(digest_item) != LMD_TYPE_STRING) {
        return js_throw_invalid_arg_type("digest", "string", digest_item);
    }
    if (get_type_id(callback_item) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "Function", callback_item);
    }

    Item derived = js_crypto_pbkdf2Sync(pass_item, salt_item, iter_item, keylen_item, digest_item);
    if (js_check_exception()) return ItemNull;
    if (derived.item == ITEM_NULL) {
        Item err = make_string_item_crypto("pbkdf2 failed");
        js_call_function(callback_item, ItemNull, &err, 1);
    } else {
        Item args[2] = { ItemNull, derived };
        js_call_function(callback_item, ItemNull, args, 2);
    }
    return ItemNull;
}

// ============================================================================
// hkdfSync/hkdf — RFC 5869 HMAC-based key derivation
// ============================================================================

static bool crypto_digest_name_from_item(Item digest_item, char* out, int out_cap) {
    if (!out || out_cap <= 0) return false;
    out[0] = '\0';
    if (get_type_id(digest_item) != LMD_TYPE_STRING) {
        js_throw_invalid_arg_type("digest", "string", digest_item);
        return false;
    }
    String* digest = it2s(digest_item);
    int pos = 0;
    for (int i = 0; digest && i < (int)digest->len && pos < out_cap - 1; i++) {
        char c = digest->chars[i];
        if (c == '-') continue;
        out[pos++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    out[pos] = '\0';
    return true;
}

static bool crypto_hkdf_length_from_item(Item length_item, int* out_len) {
    if (!out_len) return false;
    TypeId type = get_type_id(length_item);
    if (type != LMD_TYPE_INT && type != LMD_TYPE_FLOAT && type != LMD_TYPE_INT64) {
        return js_throw_invalid_arg_type("length", "number", length_item), false;
    }

    double value = 0.0;
    if (type == LMD_TYPE_INT) value = (double)it2i(length_item);
    else if (type == LMD_TYPE_FLOAT) value = it2d(length_item);
    else value = (double)it2l(length_item);

    if (value != value || value < 0.0 || value > 2147483647.0) {
        js_throw_out_of_range("length", ">= 0 && <= 2147483647", length_item);
        return false;
    }
    *out_len = (int)value;
    return true;
}

static bool crypto_hkdf_compute(int bits, const uint8_t* ikm, int ikm_len,
                                const uint8_t* salt, int salt_len,
                                const uint8_t* info, int info_len,
                                uint8_t* out, int out_len) {
    int hash_len = (int)digest_output_len_bits(bits);
    if (hash_len <= 0 || hash_len > 64 || out_len < 0) return false;
    if (out_len == 0) return true;

    uint8_t zero_salt[64];
    memset(zero_salt, 0, sizeof(zero_salt));
    const uint8_t* salt_bytes = salt_len > 0 && salt ? salt : zero_salt;
    size_t salt_size = salt_len > 0 ? (size_t)salt_len : (size_t)hash_len;
    const uint8_t* ikm_bytes = ikm_len > 0 && ikm ? ikm : crypto_empty_bytes;

    uint8_t prk[64];
    if (!digest_hmac_compute_bits(bits, salt_bytes, salt_size,
                                  ikm_bytes, (size_t)ikm_len,
                                  prk, (size_t)hash_len)) {
        return false;
    }

    uint8_t previous[64];
    uint8_t block_input[64 + 1024 + 1];
    int previous_len = 0;
    int generated = 0;
    int counter = 1;
    const uint8_t* info_bytes = info_len > 0 && info ? info : crypto_empty_bytes;

    while (generated < out_len && counter <= 255) {
        int input_len = 0;
        if (previous_len > 0) {
            memcpy(block_input, previous, (size_t)previous_len);
            input_len += previous_len;
        }
        if (info_len > 0) {
            memcpy(block_input + input_len, info_bytes, (size_t)info_len);
            input_len += info_len;
        }
        block_input[input_len++] = (uint8_t)counter;

        if (!digest_hmac_compute_bits(bits, prk, (size_t)hash_len,
                                      block_input, (size_t)input_len,
                                      previous, (size_t)hash_len)) {
            return false;
        }
        previous_len = hash_len;

        int copy_len = out_len - generated;
        if (copy_len > hash_len) copy_len = hash_len;
        memcpy(out + generated, previous, (size_t)copy_len);
        generated += copy_len;
        counter++;
    }
    return generated == out_len;
}

static Item crypto_hkdf_invalid_arg(const char* name, Item value) {
    return js_throw_invalid_arg_type(name, "string, ArrayBuffer, Buffer, TypedArray, DataView, or KeyObject", value);
}

extern "C" Item js_crypto_hkdfSync(Item digest_item, Item ikm_item, Item salt_item,
                                    Item info_item, Item length_item) {
    char digest_name[32];
    if (!crypto_digest_name_from_item(digest_item, digest_name, (int)sizeof(digest_name))) return ItemNull;

    uint8_t* ikm = NULL; int ikm_len = 0;
    uint8_t* salt = NULL; int salt_len = 0;
    uint8_t* info = NULL; int info_len = 0;
    if (!extract_bytes(ikm_item, &ikm, &ikm_len)) return crypto_hkdf_invalid_arg("ikm", ikm_item);
    if (!extract_bytes(salt_item, &salt, &salt_len)) {
        mem_free(ikm);
        return crypto_hkdf_invalid_arg("salt", salt_item);
    }
    if (!extract_bytes(info_item, &info, &info_len)) {
        mem_free(ikm); mem_free(salt);
        return crypto_hkdf_invalid_arg("info", info_item);
    }

    int out_len = 0;
    if (!crypto_hkdf_length_from_item(length_item, &out_len)) {
        mem_free(ikm); mem_free(salt); mem_free(info);
        return ItemNull;
    }
    if (info_len > 1024) {
        mem_free(ikm); mem_free(salt); mem_free(info);
        return js_throw_out_of_range("info", "<= 1024 bytes", info_item);
    }

    int bits = crypto_digest_bits_for_name(digest_name, true, true);
    if (bits == 0) {
        mem_free(ikm); mem_free(salt); mem_free(info);
        return js_throw_type_error_code("ERR_CRYPTO_INVALID_DIGEST", "Invalid digest");
    }

    int hash_len = (int)digest_output_len_bits(bits);
    if (out_len > hash_len * 255) {
        mem_free(ikm); mem_free(salt); mem_free(info);
        return js_throw_range_error_code("ERR_CRYPTO_INVALID_KEYLEN",
            "Invalid key length");
    }

    uint8_t* output = (uint8_t*)mem_alloc((size_t)(out_len > 0 ? out_len : 1), MEM_CAT_JS_RUNTIME);
    bool ok = crypto_hkdf_compute(bits, ikm, ikm_len, salt, salt_len, info, info_len, output, out_len);
    mem_free(ikm); mem_free(salt); mem_free(info);
    if (!ok) {
        mem_free(output);
        log_error("crypto: hkdfSync: derivation failed");
        return ItemNull;
    }

    Item result = crypto_arraybuffer_from_bytes(output, out_len);
    mem_free(output);
    return result;
}

static Item js_crypto_hkdf_emit(Item env_item) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env) return make_js_undefined_crypto();
    Item callback = env[0];
    Item result = env[1];
    Item args[2] = { ItemNull, result };
    js_call_function(callback, make_js_undefined_crypto(), args, 2);
    return make_js_undefined_crypto();
}

extern "C" Item js_crypto_hkdf(Item digest_item, Item ikm_item, Item salt_item,
                                Item info_item, Item length_item, Item callback_item) {
    Item result = js_crypto_hkdfSync(digest_item, ikm_item, salt_item, info_item, length_item);
    if (js_check_exception() || result.item == ITEM_NULL) return ItemNull;

    if (get_type_id(callback_item) != LMD_TYPE_FUNC) {
        return js_throw_invalid_arg_type("callback", "Function", callback_item);
    }

    Item* env = js_alloc_env(2);
    env[0] = callback_item;
    env[1] = result;
    Item fn = js_new_closure((void*)js_crypto_hkdf_emit, 0, env, 2);
    js_next_tick_enqueue(fn);
    return make_js_undefined_crypto();
}

// ============================================================================
// scryptSync(password, salt, keylen, options?) → Buffer
// Pure implementation: scrypt = PBKDF2-HMAC-SHA256 + Salsa20/8 + ROMix
// ============================================================================

// Salsa20/8 core (8 rounds of Salsa20)
static void salsa20_8_core(uint32_t B[16]) {
    uint32_t x[16];
    memcpy(x, B, 64);

    for (int i = 0; i < 8; i += 2) {
        // column round
        x[ 4] ^= ((x[ 0]+x[12]) << 7)  | ((x[ 0]+x[12]) >> 25);
        x[ 8] ^= ((x[ 4]+x[ 0]) << 9)  | ((x[ 4]+x[ 0]) >> 23);
        x[12] ^= ((x[ 8]+x[ 4]) << 13) | ((x[ 8]+x[ 4]) >> 19);
        x[ 0] ^= ((x[12]+x[ 8]) << 18) | ((x[12]+x[ 8]) >> 14);

        x[ 9] ^= ((x[ 5]+x[ 1]) << 7)  | ((x[ 5]+x[ 1]) >> 25);
        x[13] ^= ((x[ 9]+x[ 5]) << 9)  | ((x[ 9]+x[ 5]) >> 23);
        x[ 1] ^= ((x[13]+x[ 9]) << 13) | ((x[13]+x[ 9]) >> 19);
        x[ 5] ^= ((x[ 1]+x[13]) << 18) | ((x[ 1]+x[13]) >> 14);

        x[14] ^= ((x[10]+x[ 6]) << 7)  | ((x[10]+x[ 6]) >> 25);
        x[ 2] ^= ((x[14]+x[10]) << 9)  | ((x[14]+x[10]) >> 23);
        x[ 6] ^= ((x[ 2]+x[14]) << 13) | ((x[ 2]+x[14]) >> 19);
        x[10] ^= ((x[ 6]+x[ 2]) << 18) | ((x[ 6]+x[ 2]) >> 14);

        x[ 3] ^= ((x[15]+x[11]) << 7)  | ((x[15]+x[11]) >> 25);
        x[ 7] ^= ((x[ 3]+x[15]) << 9)  | ((x[ 3]+x[15]) >> 23);
        x[11] ^= ((x[ 7]+x[ 3]) << 13) | ((x[ 7]+x[ 3]) >> 19);
        x[15] ^= ((x[11]+x[ 7]) << 18) | ((x[11]+x[ 7]) >> 14);

        // row round
        x[ 1] ^= ((x[ 0]+x[ 3]) << 7)  | ((x[ 0]+x[ 3]) >> 25);
        x[ 2] ^= ((x[ 1]+x[ 0]) << 9)  | ((x[ 1]+x[ 0]) >> 23);
        x[ 3] ^= ((x[ 2]+x[ 1]) << 13) | ((x[ 2]+x[ 1]) >> 19);
        x[ 0] ^= ((x[ 3]+x[ 2]) << 18) | ((x[ 3]+x[ 2]) >> 14);

        x[ 6] ^= ((x[ 5]+x[ 4]) << 7)  | ((x[ 5]+x[ 4]) >> 25);
        x[ 7] ^= ((x[ 6]+x[ 5]) << 9)  | ((x[ 6]+x[ 5]) >> 23);
        x[ 4] ^= ((x[ 7]+x[ 6]) << 13) | ((x[ 7]+x[ 6]) >> 19);
        x[ 5] ^= ((x[ 4]+x[ 7]) << 18) | ((x[ 4]+x[ 7]) >> 14);

        x[11] ^= ((x[10]+x[ 9]) << 7)  | ((x[10]+x[ 9]) >> 25);
        x[ 8] ^= ((x[11]+x[10]) << 9)  | ((x[11]+x[10]) >> 23);
        x[ 9] ^= ((x[ 8]+x[11]) << 13) | ((x[ 8]+x[11]) >> 19);
        x[10] ^= ((x[ 9]+x[ 8]) << 18) | ((x[ 9]+x[ 8]) >> 14);

        x[12] ^= ((x[15]+x[14]) << 7)  | ((x[15]+x[14]) >> 25);
        x[13] ^= ((x[12]+x[15]) << 9)  | ((x[12]+x[15]) >> 23);
        x[14] ^= ((x[13]+x[12]) << 13) | ((x[13]+x[12]) >> 19);
        x[15] ^= ((x[14]+x[13]) << 18) | ((x[14]+x[13]) >> 14);
    }

    for (int i = 0; i < 16; i++) B[i] += x[i];
}

// BlockMix: scryptBlockMix with r blocks
static void scrypt_block_mix(uint32_t* B, uint32_t* Y, int r) {
    int block_count = 2 * r;
    uint32_t X[16];
    memcpy(X, &B[(block_count - 1) * 16], 64);

    for (int i = 0; i < block_count; i++) {
        for (int j = 0; j < 16; j++) X[j] ^= B[i * 16 + j];
        salsa20_8_core(X);
        memcpy(&Y[i * 16], X, 64);
    }

    // interleave: even blocks first, then odd blocks
    for (int i = 0; i < r; i++) {
        memcpy(&B[i * 16], &Y[(2*i) * 16], 64);
        memcpy(&B[(r + i) * 16], &Y[(2*i + 1) * 16], 64);
    }
}

// ROMix
static void scrypt_romix(uint32_t* B, int r, int N) {
    int block_words = 32 * r; // 2*r blocks of 16 uint32_t
    size_t block_bytes = (size_t)block_words * 4;
    uint32_t* V = (uint32_t*)mem_alloc(block_bytes * (size_t)N, MEM_CAT_JS_RUNTIME);
    uint32_t* Y = (uint32_t*)mem_alloc(block_bytes, MEM_CAT_JS_RUNTIME);

    for (int i = 0; i < N; i++) {
        memcpy(&V[(size_t)i * block_words], B, block_bytes);
        scrypt_block_mix(B, Y, r);
    }

    for (int i = 0; i < N; i++) {
        // integerify: take last block's first uint32 mod N
        int j = (int)(B[block_words - 16] % (uint32_t)N);
        for (int k = 0; k < block_words; k++) B[k] ^= V[(size_t)j * block_words + k];
        scrypt_block_mix(B, Y, r);
    }

    mem_free(V);
    mem_free(Y);
}

extern "C" Item js_crypto_scryptSync(Item pass_item, Item salt_item, Item keylen_item, Item opts_item) {
    uint8_t* pass = NULL; int pass_len = 0;
    uint8_t* salt = NULL; int salt_len = 0;

    if (!extract_bytes(pass_item, &pass, &pass_len)) return ItemNull;
    if (!extract_bytes(salt_item, &salt, &salt_len)) { mem_free(pass); return ItemNull; }

    int keylen = (int)it2i(keylen_item);
    if (keylen < 1 || keylen > 1024) {
        log_error("crypto: scryptSync: invalid keylen=%d", keylen);
        mem_free(pass); mem_free(salt);
        return ItemNull;
    }

    // defaults matching Node.js
    int N = 16384, r = 8, p = 1;
    if (get_type_id(opts_item) == LMD_TYPE_MAP) {
        Item n_item = js_property_get(opts_item, make_string_item_crypto("N"));
        Item cost_item = js_property_get(opts_item, make_string_item_crypto("cost"));
        Item r_item = js_property_get(opts_item, make_string_item_crypto("r"));
        Item bsize_item = js_property_get(opts_item, make_string_item_crypto("blockSize"));
        Item p_item = js_property_get(opts_item, make_string_item_crypto("p"));
        Item par_item = js_property_get(opts_item, make_string_item_crypto("parallelization"));

        if (n_item.item != ITEM_NULL) N = (int)it2i(n_item);
        else if (cost_item.item != ITEM_NULL) N = (int)it2i(cost_item);
        if (r_item.item != ITEM_NULL) r = (int)it2i(r_item);
        else if (bsize_item.item != ITEM_NULL) r = (int)it2i(bsize_item);
        if (p_item.item != ITEM_NULL) p = (int)it2i(p_item);
        else if (par_item.item != ITEM_NULL) p = (int)it2i(par_item);
    }

    // validate N is power of 2
    if (N < 2 || (N & (N - 1)) != 0) {
        log_error("crypto: scryptSync: N must be power of 2, got %d", N);
        mem_free(pass); mem_free(salt);
        return ItemNull;
    }

    // limit memory: N * r * 128 * p  (e.g. 16384*8*128*1 = 16 MB)
    size_t mem_needed = (size_t)N * (size_t)r * 128 * (size_t)p;
    if (mem_needed > 256 * 1024 * 1024) {
        log_error("crypto: scryptSync: memory limit exceeded (%zu bytes)", mem_needed);
        mem_free(pass); mem_free(salt);
        return ItemNull;
    }

    // step 1: PBKDF2-HMAC-SHA256 to derive B (p blocks of 128*r bytes)
    int block_size = 128 * r;
    int B_len = p * block_size;
    uint8_t* B = (uint8_t*)mem_alloc((size_t)B_len, MEM_CAT_JS_RUNTIME);

    bool ok = digest_pbkdf2_hmac_bits(DIGEST_SHA256, pass, (size_t)pass_len,
                                      salt, (size_t)salt_len, 1,
                                      B, (size_t)B_len);
    if (!ok) {
        log_error("crypto: scryptSync: initial PBKDF2 failed");
        mem_free(pass); mem_free(salt); mem_free(B);
        return ItemNull;
    }

    // step 2: ROMix each block
    for (int i = 0; i < p; i++) {
        scrypt_romix((uint32_t*)(B + i * block_size), r, N);
    }

    // step 3: PBKDF2-HMAC-SHA256 with B as salt to derive final key
    uint8_t* output = (uint8_t*)mem_alloc((size_t)keylen, MEM_CAT_JS_RUNTIME);
    ok = digest_pbkdf2_hmac_bits(DIGEST_SHA256, pass, (size_t)pass_len,
                                 B, (size_t)B_len, 1,
                                 output, (size_t)keylen);
    mem_free(pass);
    mem_free(salt);
    mem_free(B);

    if (!ok) {
        log_error("crypto: scryptSync: final PBKDF2 failed");
        mem_free(output);
        return ItemNull;
    }

    Item result = js_typed_array_new(JS_TYPED_UINT8, keylen);
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (ta && ta->data) memcpy(ta->data, output, (size_t)keylen);
    mem_free(output);
    return result;
}

// ============================================================================
// getCiphers() → array of supported cipher algorithm names
// ============================================================================

extern "C" Item js_crypto_getCiphers(void) {
    Item arr = js_array_new(0);
    js_array_push(arr, make_string_item_crypto("aes-128-ecb"));
    js_array_push(arr, make_string_item_crypto("aes-192-ecb"));
    js_array_push(arr, make_string_item_crypto("aes-256-ecb"));
    js_array_push(arr, make_string_item_crypto("aes-128-cbc"));
    js_array_push(arr, make_string_item_crypto("aes-192-cbc"));
    js_array_push(arr, make_string_item_crypto("aes-256-cbc"));
    js_array_push(arr, make_string_item_crypto("aes-128-ctr"));
    js_array_push(arr, make_string_item_crypto("aes-192-ctr"));
    js_array_push(arr, make_string_item_crypto("aes-256-ctr"));
    js_array_push(arr, make_string_item_crypto("aes-128-gcm"));
    js_array_push(arr, make_string_item_crypto("aes-192-gcm"));
    js_array_push(arr, make_string_item_crypto("aes-256-gcm"));
    js_array_push(arr, make_string_item_crypto("id-aes128-wrap"));
    js_array_push(arr, make_string_item_crypto("des-ede3-cbc"));
    return arr;
}

struct CryptoCipherInfo {
    const char* name;
    int nid;
    int block_size;
    int iv_length;
    int key_length;
    const char* mode;
};

static const CryptoCipherInfo crypto_cipher_infos[] = {
    {"aes-128-ecb", 418, 16, 0, 16, "ecb"},
    {"aes-192-ecb", 422, 16, 0, 24, "ecb"},
    {"aes-256-ecb", 426, 16, 0, 32, "ecb"},
    {"aes-128-cbc", 419, 16, 16, 16, "cbc"},
    {"aes-192-cbc", 423, 16, 16, 24, "cbc"},
    {"aes-256-cbc", 427, 16, 16, 32, "cbc"},
    {"aes-128-ctr", 904,  1, 16, 16, "ctr"},
    {"aes-192-ctr", 905,  1, 16, 24, "ctr"},
    {"aes-256-ctr", 906,  1, 16, 32, "ctr"},
    {"aes-128-gcm", 895,  1, 12, 16, "gcm"},
    {"aes-192-gcm", 898,  1, 12, 24, "gcm"},
    {"aes-256-gcm", 901,  1, 12, 32, "gcm"},
    {"id-aes128-wrap", 788, 8, 8, 16, "wrap"},
    {"des-ede3-cbc", 44, 8, 8, 24, "cbc"},
};

static const CryptoCipherInfo* crypto_find_cipher_info_by_name(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(name_item);
    if (!s) return NULL;
    for (int i = 0; i < (int)(sizeof(crypto_cipher_infos) / sizeof(crypto_cipher_infos[0])); i++) {
        const CryptoCipherInfo* info = &crypto_cipher_infos[i];
        size_t len = strlen(info->name);
        if (s->len == len && memcmp(s->chars, info->name, len) == 0) return info;
    }
    return NULL;
}

static bool crypto_item_to_int_exact(Item item, int* out_value) {
    if (!out_value) return false;
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT) {
        *out_value = (int)it2i(item);
        return true;
    }
    if (type == LMD_TYPE_INT64) {
        int64_t value = it2l(item);
        if (value < -2147483648LL || value > 2147483647LL) return false;
        *out_value = (int)value;
        return true;
    }
    if (type == LMD_TYPE_FLOAT) {
        double value = it2d(item);
        if (value != value || value < -2147483648.0 || value > 2147483647.0) return false;
        int int_value = (int)value;
        if ((double)int_value != value) return false;
        *out_value = int_value;
        return true;
    }
    return false;
}

static const CryptoCipherInfo* crypto_find_cipher_info_by_nid(Item nid_item) {
    int nid = 0;
    if (!crypto_item_to_int_exact(nid_item, &nid)) return NULL;
    for (int i = 0; i < (int)(sizeof(crypto_cipher_infos) / sizeof(crypto_cipher_infos[0])); i++) {
        const CryptoCipherInfo* info = &crypto_cipher_infos[i];
        if (info->nid == nid) return info;
    }
    return NULL;
}

static bool crypto_validate_cipher_info_options(Item options_item, const CryptoCipherInfo* info) {
    if (crypto_item_is_undefined(options_item)) return true;
    if (get_type_id(options_item) != LMD_TYPE_MAP) {
        js_throw_invalid_arg_type("options", "Object", options_item);
        return false;
    }

    Item key_len_item = js_property_get(options_item, make_string_item_crypto("keyLength"));
    if (!crypto_item_is_undefined(key_len_item)) {
        int key_len = 0;
        if (!crypto_item_to_int_exact(key_len_item, &key_len)) {
            js_throw_invalid_arg_type("options.keyLength", "number", key_len_item);
            return false;
        }
        if (!info || key_len != info->key_length) return false;
    }

    Item iv_len_item = js_property_get(options_item, make_string_item_crypto("ivLength"));
    if (!crypto_item_is_undefined(iv_len_item)) {
        int iv_len = 0;
        if (!crypto_item_to_int_exact(iv_len_item, &iv_len)) {
            js_throw_invalid_arg_type("options.ivLength", "number", iv_len_item);
            return false;
        }
        if (!info || iv_len != info->iv_length) return false;
    }
    return true;
}

static Item crypto_cipher_info_to_object(const CryptoCipherInfo* info) {
    if (!info) return make_js_undefined_crypto();
    Item obj = js_new_object();
    js_property_set(obj, make_string_item_crypto("name"), make_string_item_crypto(info->name));
    js_property_set(obj, make_string_item_crypto("nid"), (Item){.item = i2it(info->nid)});
    js_property_set(obj, make_string_item_crypto("blockSize"), (Item){.item = i2it(info->block_size)});
    js_property_set(obj, make_string_item_crypto("ivLength"), (Item){.item = i2it(info->iv_length)});
    js_property_set(obj, make_string_item_crypto("keyLength"), (Item){.item = i2it(info->key_length)});
    js_property_set(obj, make_string_item_crypto("mode"), make_string_item_crypto(info->mode));
    return obj;
}

extern "C" Item js_crypto_getCipherInfo(Item cipher_item, Item options_item) {
    TypeId cipher_type = get_type_id(cipher_item);
    const CryptoCipherInfo* info = NULL;
    if (cipher_type == LMD_TYPE_STRING) {
        info = crypto_find_cipher_info_by_name(cipher_item);
    } else if (cipher_type == LMD_TYPE_INT || cipher_type == LMD_TYPE_FLOAT || cipher_type == LMD_TYPE_INT64) {
        info = crypto_find_cipher_info_by_nid(cipher_item);
    } else {
        return js_throw_invalid_arg_type("nameOrNid", "string or number", cipher_item);
    }

    if (!crypto_validate_cipher_info_options(options_item, info)) {
        if (js_check_exception()) return ItemNull;
        return make_js_undefined_crypto();
    }
    return crypto_cipher_info_to_object(info);
}

// ============================================================================
// Web Crypto subtle API (subset: digest, encrypt, decrypt)
// All return Promises (resolved synchronously for now)
// ============================================================================

// subtle.digest(algorithm, data) → Promise<ArrayBuffer>
// algorithm can be string "SHA-256" or {name: "SHA-256"}
extern "C" Item js_subtle_digest(Item alg_item, Item data_item) {
    // resolve algorithm name
    char alg_name[32] = {0};
    if (get_type_id(alg_item) == LMD_TYPE_STRING) {
        String* s = it2s(alg_item);
        int len = (int)s->len < 31 ? (int)s->len : 31;
        memcpy(alg_name, s->chars, (size_t)len);
    } else if (get_type_id(alg_item) == LMD_TYPE_MAP) {
        Item name = js_property_get(alg_item, make_string_item_crypto("name"));
        if (get_type_id(name) == LMD_TYPE_STRING) {
            String* s = it2s(name);
            int len = (int)s->len < 31 ? (int)s->len : 31;
            memcpy(alg_name, s->chars, (size_t)len);
        }
    }

    // normalize: "SHA-256" → "sha256"
    char normalized[32] = {0};
    int ni = 0;
    for (int i = 0; alg_name[i] && ni < 31; i++) {
        char c = alg_name[i];
        if (c == '-') continue;
        normalized[ni++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }

    // extract data
    const uint8_t* buf = NULL;
    int buf_len = 0;
    bool need_free = false;
    if (js_is_typed_array(data_item)) {
        get_uint8_buffer(data_item, &buf, &buf_len);
    } else if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        buf = (const uint8_t*)s->chars;
        buf_len = (int)s->len;
    }
    if (!buf) { buf = (const uint8_t*)""; buf_len = 0; }

    uint8_t hash[64];
    int hash_len = 0;
    int digest_bits = crypto_digest_bits_for_name(normalized, true, false);
    hash_len = (int)digest_output_len_bits(digest_bits);
    if (hash_len > 0) {
        if (!crypto_digest_compute_bits(digest_bits, buf, 0, buf_len, hash)) {
            hash_len = 0;
        }
    }
    (void)need_free;

    if (hash_len == 0) {
        log_error("crypto: subtle.digest: unsupported algorithm: %s", alg_name);
        return ItemNull;
    }

    Item result = js_typed_array_new(JS_TYPED_UINT8, hash_len);
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (ta && ta->data) memcpy(ta->data, hash, (size_t)hash_len);

    // wrap in resolved Promise
    return js_promise_resolve(result);
}

// subtle.encrypt({name, iv}, key, data) → Promise<ArrayBuffer>
extern "C" Item js_subtle_encrypt(Item alg_item, Item key_item, Item data_item) {
    // extract algorithm name and IV from options object
    if (get_type_id(alg_item) != LMD_TYPE_MAP) return ItemNull;
    Item name_item = js_property_get(alg_item, make_string_item_crypto("name"));
    Item iv_item = js_property_get(alg_item, make_string_item_crypto("iv"));

    if (get_type_id(name_item) != LMD_TYPE_STRING) return ItemNull;
    String* name_s = it2s(name_item);

    // determine cipher
    char name_buf[32] = {0};
    int nlen = (int)name_s->len < 31 ? (int)name_s->len : 31;
    memcpy(name_buf, name_s->chars, (size_t)nlen);

    // normalize: "AES-CBC" → "aes-cbc", "AES-GCM" → "aes-gcm", "AES-CTR" → "aes-ctr"
    for (int i = 0; name_buf[i]; i++) {
        if (name_buf[i] >= 'A' && name_buf[i] <= 'Z')
            name_buf[i] = (char)(name_buf[i] + 32);
    }

    uint8_t* key_bytes = NULL; int key_len = 0;
    uint8_t* iv_bytes = NULL; int iv_len = 0;
    const uint8_t* data_buf = NULL; int data_len = 0;

    if (!extract_bytes(key_item, &key_bytes, &key_len)) return ItemNull;
    if (!extract_bytes(iv_item, &iv_bytes, &iv_len)) { mem_free(key_bytes); return ItemNull; }
    if (js_is_typed_array(data_item)) {
        get_uint8_buffer(data_item, &data_buf, &data_len);
    } else if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        data_buf = (const uint8_t*)s->chars;
        data_len = (int)s->len;
    }

    // build full algorithm name with key size (e.g. "aes-256-cbc")
    char full_alg[32];
    snprintf(full_alg, sizeof(full_alg), "aes-%d-%s", key_len * 8, name_buf + 4); // skip "aes-"

    // if name is just "aes-cbc" etc, resolve_cipher_type handles it
    if (strncmp(name_buf, "aes-", 4) != 0) {
        // try as full name
        snprintf(full_alg, sizeof(full_alg), "%s", name_buf);
    }

    Item cipher_obj = create_cipher_object(full_alg, true, key_bytes, key_len, iv_bytes, iv_len);
    if (cipher_obj.item == ITEM_NULL) return ItemNull;

    // set AAD if present (for GCM)
    Item aad = js_property_get(alg_item, make_string_item_crypto("additionalData"));
    if (aad.item != ITEM_NULL) {
        Item setAAD_fn = js_property_get(cipher_obj, make_string_item_crypto("setAAD"));
        js_call_function(setAAD_fn, cipher_obj, &aad, 1);
    }

    // update with data
    Item update_fn = js_property_get(cipher_obj, make_string_item_crypto("update"));
    Item update_result = js_call_function(update_fn, cipher_obj, &data_item, 1);

    // final
    Item final_fn = js_property_get(cipher_obj, make_string_item_crypto("final"));
    Item final_result = js_call_function(final_fn, cipher_obj, NULL, 0);

    // concatenate update_result + final_result
    int update_len = 0, final_len_val = 0;
    const uint8_t *u_buf = NULL, *f_buf = NULL;
    if (js_is_typed_array(update_result)) get_uint8_buffer(update_result, &u_buf, &update_len);
    if (js_is_typed_array(final_result)) get_uint8_buffer(final_result, &f_buf, &final_len_val);

    int total = update_len + final_len_val;
    Item result = js_typed_array_new(JS_TYPED_UINT8, total);
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (ta && ta->data) {
        if (u_buf && update_len > 0) memcpy(ta->data, u_buf, (size_t)update_len);
        if (f_buf && final_len_val > 0) memcpy((uint8_t*)ta->data + update_len, f_buf, (size_t)final_len_val);
    }

    return js_promise_resolve(result);
}

// subtle.decrypt({name, iv}, key, data) → Promise<ArrayBuffer>
extern "C" Item js_subtle_decrypt(Item alg_item, Item key_item, Item data_item) {
    if (get_type_id(alg_item) != LMD_TYPE_MAP) return ItemNull;
    Item name_item = js_property_get(alg_item, make_string_item_crypto("name"));
    Item iv_item = js_property_get(alg_item, make_string_item_crypto("iv"));

    if (get_type_id(name_item) != LMD_TYPE_STRING) return ItemNull;
    String* name_s = it2s(name_item);

    char name_buf[32] = {0};
    int nlen = (int)name_s->len < 31 ? (int)name_s->len : 31;
    memcpy(name_buf, name_s->chars, (size_t)nlen);
    for (int i = 0; name_buf[i]; i++) {
        if (name_buf[i] >= 'A' && name_buf[i] <= 'Z')
            name_buf[i] = (char)(name_buf[i] + 32);
    }

    uint8_t* key_bytes = NULL; int key_len = 0;
    uint8_t* iv_bytes = NULL; int iv_len = 0;
    const uint8_t* data_buf = NULL; int data_len = 0;

    if (!extract_bytes(key_item, &key_bytes, &key_len)) return ItemNull;
    if (!extract_bytes(iv_item, &iv_bytes, &iv_len)) { mem_free(key_bytes); return ItemNull; }
    if (js_is_typed_array(data_item)) {
        get_uint8_buffer(data_item, &data_buf, &data_len);
    } else if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        data_buf = (const uint8_t*)s->chars;
        data_len = (int)s->len;
    }

    char full_alg[32];
    if (strncmp(name_buf, "aes-", 4) == 0 && strlen(name_buf) < 12) {
        snprintf(full_alg, sizeof(full_alg), "aes-%d-%s", key_len * 8, name_buf + 4);
    } else {
        snprintf(full_alg, sizeof(full_alg), "%s", name_buf);
    }

    Item decipher_obj = create_cipher_object(full_alg, false, key_bytes, key_len, iv_bytes, iv_len);
    if (decipher_obj.item == ITEM_NULL) return ItemNull;

    // set auth tag for GCM if present
    Item tag = js_property_get(alg_item, make_string_item_crypto("tag"));
    if (tag.item != ITEM_NULL) {
        Item setAuthTag_fn = js_property_get(decipher_obj, make_string_item_crypto("setAuthTag"));
        js_call_function(setAuthTag_fn, decipher_obj, &tag, 1);
    }
    Item aad = js_property_get(alg_item, make_string_item_crypto("additionalData"));
    if (aad.item != ITEM_NULL) {
        Item setAAD_fn = js_property_get(decipher_obj, make_string_item_crypto("setAAD"));
        js_call_function(setAAD_fn, decipher_obj, &aad, 1);
    }

    Item update_fn = js_property_get(decipher_obj, make_string_item_crypto("update"));
    Item update_result = js_call_function(update_fn, decipher_obj, &data_item, 1);

    Item final_fn = js_property_get(decipher_obj, make_string_item_crypto("final"));
    Item final_result = js_call_function(final_fn, decipher_obj, NULL, 0);

    int update_len = 0, final_len_val = 0;
    const uint8_t *u_buf = NULL, *f_buf = NULL;
    if (js_is_typed_array(update_result)) get_uint8_buffer(update_result, &u_buf, &update_len);
    if (js_is_typed_array(final_result)) get_uint8_buffer(final_result, &f_buf, &final_len_val);

    int total = update_len + final_len_val;
    Item result = js_typed_array_new(JS_TYPED_UINT8, total);
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (ta && ta->data) {
        if (u_buf && update_len > 0) memcpy(ta->data, u_buf, (size_t)update_len);
        if (f_buf && final_len_val > 0) memcpy((uint8_t*)ta->data + update_len, f_buf, (size_t)final_len_val);
    }

    return js_promise_resolve(result);
}

// ============================================================================
// crypto Module Namespace
// ============================================================================

static void crypto_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item_crypto(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

static void crypto_set_hidden_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item_crypto(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
    js_mark_non_enumerable(ns, key);
}

extern "C" Item js_get_crypto_namespace(void) {
    if (crypto_namespace.item != 0) return crypto_namespace;

    crypto_namespace = js_new_object();

    crypto_set_method(crypto_namespace, "createHash",         (void*)js_crypto_createHash, 1);
    crypto_set_method(crypto_namespace, "hash",               (void*)js_crypto_hash, 3);
    crypto_set_method(crypto_namespace, "createHmac",         (void*)js_crypto_createHmac, 2);
    crypto_set_method(crypto_namespace, "createSign",         (void*)js_crypto_createSign, 1);
    crypto_set_method(crypto_namespace, "createVerify",       (void*)js_crypto_createVerify, 1);
    crypto_set_method(crypto_namespace, "createCipheriv",     (void*)js_crypto_createCipheriv, 3);
    crypto_set_method(crypto_namespace, "createDecipheriv",   (void*)js_crypto_createDecipheriv, 3);
    crypto_set_method(crypto_namespace, "argon2",             (void*)js_crypto_argon2_unsupported, 0);
    crypto_set_method(crypto_namespace, "argon2Sync",         (void*)js_crypto_argon2_unsupported, 0);
    crypto_set_method(crypto_namespace, "randomBytes",        (void*)js_crypto_randomBytes, 2);
    crypto_set_hidden_method(crypto_namespace, "pseudoRandomBytes", (void*)js_crypto_pseudoRandomBytes, 2);
    crypto_set_hidden_method(crypto_namespace, "prng",              (void*)js_crypto_randomBytes, 2);
    crypto_set_hidden_method(crypto_namespace, "rng",               (void*)js_crypto_randomBytes, 2);
    crypto_set_method(crypto_namespace, "randomFillSync",     (void*)js_crypto_randomFillSync, 3);
    crypto_set_method(crypto_namespace, "randomFill",         (void*)js_crypto_randomFill, 4);
    crypto_set_method(crypto_namespace, "getRandomValues",    (void*)js_crypto_getRandomValues, 1);
    crypto_set_method(crypto_namespace, "randomUUID",         (void*)js_crypto_randomUUID, 1);
    crypto_set_method(crypto_namespace, "randomUUIDv7",       (void*)js_crypto_randomUUIDv7, 1);
    crypto_set_method(crypto_namespace, "randomInt",          (void*)js_crypto_randomInt, 3);
    crypto_set_method(crypto_namespace, "getFips",            (void*)js_crypto_getFips, 0);
    crypto_set_method(crypto_namespace, "getHashes",          (void*)js_crypto_getHashes, 0);
    crypto_set_method(crypto_namespace, "getCurves",          (void*)js_crypto_getCurves, 0);
    crypto_set_method(crypto_namespace, "getCiphers",         (void*)js_crypto_getCiphers, 0);
    crypto_set_method(crypto_namespace, "getCipherInfo",      (void*)js_crypto_getCipherInfo, 2);
    crypto_set_method(crypto_namespace, "createDiffieHellman", (void*)js_crypto_createDiffieHellman, 4);
    crypto_set_method(crypto_namespace, "createDiffieHellmanGroup", (void*)js_crypto_createDiffieHellmanGroup, 1);
    crypto_set_method(crypto_namespace, "getDiffieHellman",   (void*)js_crypto_getDiffieHellman, 1);
    crypto_set_method(crypto_namespace, "createECDH",         (void*)js_crypto_createECDH, 1);
    crypto_set_method(crypto_namespace, "timingSafeEqual",    (void*)js_crypto_timingSafeEqual, 2);
    crypto_set_method(crypto_namespace, "pbkdf2Sync",         (void*)js_crypto_pbkdf2Sync, 5);
    crypto_set_method(crypto_namespace, "pbkdf2",             (void*)js_crypto_pbkdf2, 6);
    crypto_set_method(crypto_namespace, "hkdfSync",           (void*)js_crypto_hkdfSync, 5);
    crypto_set_method(crypto_namespace, "hkdf",               (void*)js_crypto_hkdf, 6);
    crypto_set_method(crypto_namespace, "scryptSync",         (void*)js_crypto_scryptSync, 4);
    crypto_set_method(crypto_namespace, "createSecretKey",    (void*)js_crypto_createSecretKey, 2);
    crypto_set_method(crypto_namespace, "generateKeySync",    (void*)js_crypto_generateKeySync, 2);
    crypto_set_method(crypto_namespace, "generateKey",        (void*)js_crypto_generateKey, 3);

    // subtle Web Crypto API (subset)
    Item subtle = js_new_object();
    crypto_set_method(subtle, "digest",  (void*)js_subtle_digest, 2);
    crypto_set_method(subtle, "encrypt", (void*)js_subtle_encrypt, 3);
    crypto_set_method(subtle, "decrypt", (void*)js_subtle_decrypt, 3);
    js_property_set(crypto_namespace, make_string_item_crypto("subtle"), subtle);

    // crypto.constants — OpenSSL-compatible constants
    Item constants = js_new_object();
    // SSL/TLS
    js_property_set(constants, make_string_item_crypto("SSL_OP_ALL"), (Item){.item = i2it(0)});
    js_property_set(constants, make_string_item_crypto("SSL_OP_NO_SSLv2"), (Item){.item = i2it(0x01000000)});
    js_property_set(constants, make_string_item_crypto("SSL_OP_NO_SSLv3"), (Item){.item = i2it(0x02000000)});
    js_property_set(constants, make_string_item_crypto("SSL_OP_NO_TLSv1"), (Item){.item = i2it(0x04000000)});
    // DH/RSA padding
    js_property_set(constants, make_string_item_crypto("RSA_PKCS1_PADDING"), (Item){.item = i2it(1)});
    js_property_set(constants, make_string_item_crypto("RSA_PKCS1_OAEP_PADDING"), (Item){.item = i2it(4)});
    js_property_set(constants, make_string_item_crypto("RSA_NO_PADDING"), (Item){.item = i2it(3)});
    // point conversion
    js_property_set(constants, make_string_item_crypto("POINT_CONVERSION_COMPRESSED"), (Item){.item = i2it(2)});
    js_property_set(constants, make_string_item_crypto("POINT_CONVERSION_UNCOMPRESSED"), (Item){.item = i2it(4)});
    js_property_set(crypto_namespace, make_string_item_crypto("constants"), constants);

    // class constructors as stubs (for typeof/instanceof checks)
    js_property_set(crypto_namespace, make_string_item_crypto("Hash"),
        js_new_function((void*)js_crypto_createHash, 1));
    js_property_set(crypto_namespace, make_string_item_crypto("Hmac"),
        js_new_function((void*)js_crypto_createHmac, 2));
    js_property_set(crypto_namespace, make_string_item_crypto("Sign"),
        js_new_function((void*)js_crypto_createSign, 1));
    js_property_set(crypto_namespace, make_string_item_crypto("Verify"),
        js_new_function((void*)js_crypto_createVerify, 1));
    js_property_set(crypto_namespace, make_string_item_crypto("Cipher"),
        js_new_function((void*)js_crypto_createCipheriv, 3));
    js_property_set(crypto_namespace, make_string_item_crypto("Decipher"),
        js_new_function((void*)js_crypto_createDecipheriv, 3));
    js_property_set(crypto_namespace, make_string_item_crypto("Cipheriv"),
        js_new_function((void*)js_crypto_createCipheriv, 3));
    js_property_set(crypto_namespace, make_string_item_crypto("Decipheriv"),
        js_new_function((void*)js_crypto_createDecipheriv, 3));
    js_property_set(crypto_namespace, make_string_item_crypto("DiffieHellman"),
        js_new_function((void*)js_crypto_createDiffieHellman, 4));
    js_property_set(crypto_namespace, make_string_item_crypto("DiffieHellmanGroup"),
        js_new_function((void*)js_crypto_createDiffieHellmanGroup, 1));
    js_property_set(crypto_namespace, make_string_item_crypto("ECDH"),
        js_new_function((void*)js_crypto_createECDH, 1));

    Item default_key = make_string_item_crypto("default");
    js_property_set(crypto_namespace, default_key, crypto_namespace);

    return crypto_namespace;
}

extern "C" void js_crypto_reset(void) {
    crypto_namespace = (Item){0};
}
