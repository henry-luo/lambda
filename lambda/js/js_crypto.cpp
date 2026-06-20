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
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include <cstring>
#include "../../lib/mem.h"
#include "../../lib/hex.h"
#include "../../lib/base64.h"
#include "../../lib/uuid.h"
#include "../../lib/digest.h"

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

extern "C" Item js_crypto_randomBytes(Item size_item) {
    int size = (int)it2i(size_item);
    if (size <= 0 || size > 65536) {
        return js_throw_out_of_range("size", ">= 0 && <= 65536", size_item);
    }
    Item result = js_typed_array_new(JS_TYPED_UINT8, size);
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (!ta || !ta->data) return ItemNull;
    if (!crypto_random_bytes((uint8_t*)ta->data, (size_t)size)) {
        log_error("crypto: randomBytes: entropy source failed");
        return ItemNull;
    }
    return result;
}

static bool crypto_to_int_index(Item item, int default_value, int* out_value, const char* name) {
    if (!out_value) return false;
    if (crypto_item_is_undefined(item)) {
        *out_value = default_value;
        return true;
    }

    Item num = js_to_number(item);
    if (js_check_exception()) return false;

    double value = 0.0;
    TypeId num_type = get_type_id(num);
    if (num_type == LMD_TYPE_INT) {
        value = (double)it2i(num);
    } else if (num_type == LMD_TYPE_FLOAT) {
        value = it2d(num);
    } else if (num_type == LMD_TYPE_INT64) {
        value = (double)it2l(num);
    } else {
        js_throw_invalid_arg_type(name, "number", item);
        return false;
    }

    if (value != value || value < -2147483648.0 || value > 2147483647.0) {
        js_throw_out_of_range(name, "a finite integer", item);
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
    if (!crypto_to_int_index(offset_item, 0, &offset, "offset")) return ItemNull;
    if (offset < 0 || offset > byte_len) {
        return js_throw_out_of_range("offset", ">= 0 && <= buf.byteLength", offset_item);
    }

    int size = byte_len - offset;
    if (!crypto_to_int_index(size_item, size, &size, "size")) return ItemNull;
    if (size < 0 || size > byte_len - offset) {
        return js_throw_out_of_range("size", ">= 0 && <= buf.byteLength - offset", size_item);
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
// randomInt(min, max) → integer in [min, max)
// ============================================================================

extern "C" Item js_crypto_randomInt(Item min_item, Item max_item) {
    int64_t min_val = it2i(min_item);
    int64_t max_val = it2i(max_item);
    if (max_val <= min_val) return (Item){.item = i2it(min_val)};
    uint32_t rnd;
    crypto_random_bytes((uint8_t*)&rnd, sizeof(rnd));
    int64_t range = max_val - min_val;
    return (Item){.item = i2it(min_val + (int64_t)(rnd % (uint32_t)range))};
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
};

extern "C" Item js_hash_update(Item data_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__hash_ctx__"));
    if (ctx_item.item == 0) return self;
    HashCtx* ctx = (HashCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx) return self;

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        int need = ctx->data_len + (int)s->len;
        if (need > ctx->data_cap) {
            int cap = ctx->data_cap == 0 ? 1024 : ctx->data_cap;
            while (cap < need) cap *= 2;
            ctx->data = (uint8_t*)mem_realloc(ctx->data, (size_t)cap, MEM_CAT_JS_RUNTIME);
            ctx->data_cap = cap;
        }
        memcpy(ctx->data + ctx->data_len, s->chars, s->len);
        ctx->data_len += (int)s->len;
    } else if (js_is_typed_array(data_item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(data_item, &buf, &len)) {
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
    }
    return self;
}

extern "C" Item js_hash_digest(Item encoding_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__hash_ctx__"));
    if (ctx_item.item == 0) return ItemNull;
    HashCtx* ctx = (HashCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx) return ItemNull;

    uint8_t hash[64];
    int hash_len = 0;

    int digest_bits = crypto_digest_bits_for_name(ctx->alg, false, false);
    hash_len = (int)digest_output_len_bits(digest_bits);
    if (hash_len > 0) {
        if (!crypto_digest_compute_bits(digest_bits, ctx->data, 0, ctx->data_len, hash)) {
            hash_len = 0;
        }
    }

    if (ctx->data) mem_free(ctx->data);
    mem_free(ctx);
    js_property_set(self, make_string_item_crypto("__hash_ctx__"), ItemNull);

    if (hash_len == 0) return ItemNull;

    const char* enc = NULL;
    char enc_buf[32];
    if (get_type_id(encoding_item) == LMD_TYPE_STRING) {
        String* s = it2s(encoding_item);
        int len = (int)s->len < 31 ? (int)s->len : 31;
        memcpy(enc_buf, s->chars, (size_t)len);
        enc_buf[len] = '\0';
        enc = enc_buf;
    }

    if (enc && strcmp(enc, "hex") == 0) return bytes_to_hex_string(hash, hash_len);
    if (enc && strcmp(enc, "base64") == 0) return bytes_to_base64_string(hash, hash_len);

    Item result = js_typed_array_new(JS_TYPED_UINT8, hash_len);
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (ta && ta->data) memcpy(ta->data, hash, (size_t)hash_len);
    return result;
}

extern "C" Item js_crypto_createHash(Item alg_item) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) return js_throw_invalid_arg_type("algorithm", "string", alg_item);
    String* alg = it2s(alg_item);

    HashCtx* ctx = (HashCtx*)mem_calloc(1, sizeof(HashCtx), MEM_CAT_JS_RUNTIME);
    int alen = (int)alg->len < 15 ? (int)alg->len : 15;
    memcpy(ctx->alg, alg->chars, (size_t)alen);
    ctx->alg[alen] = '\0';

    Item obj = js_new_object();
    js_property_set(obj, make_string_item_crypto("__hash_ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    js_property_set(obj, make_string_item_crypto("update"),
                    js_new_function((void*)js_hash_update, 1));
    js_property_set(obj, make_string_item_crypto("digest"),
                    js_new_function((void*)js_hash_digest, 1));
    return obj;
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

    int digest_bits = crypto_digest_bits_for_name(alg_buf, true, true);
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

// map Node.js algorithm name to mbedTLS cipher type
static mbedtls_cipher_type_t resolve_cipher_type(const char* alg, int key_len) {
    if (strcmp(alg, "aes-128-cbc") == 0) return MBEDTLS_CIPHER_AES_128_CBC;
    if (strcmp(alg, "aes-192-cbc") == 0) return MBEDTLS_CIPHER_AES_192_CBC;
    if (strcmp(alg, "aes-256-cbc") == 0) return MBEDTLS_CIPHER_AES_256_CBC;
    if (strcmp(alg, "aes-128-ctr") == 0) return MBEDTLS_CIPHER_AES_128_CTR;
    if (strcmp(alg, "aes-192-ctr") == 0) return MBEDTLS_CIPHER_AES_192_CTR;
    if (strcmp(alg, "aes-256-ctr") == 0) return MBEDTLS_CIPHER_AES_256_CTR;
    if (strcmp(alg, "aes-128-gcm") == 0) return MBEDTLS_CIPHER_AES_128_GCM;
    if (strcmp(alg, "aes-192-gcm") == 0) return MBEDTLS_CIPHER_AES_192_GCM;
    if (strcmp(alg, "aes-256-gcm") == 0) return MBEDTLS_CIPHER_AES_256_GCM;
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

struct CipherCtx {
    char alg[32];
    mbedtls_cipher_context_t cipher;
    mbedtls_gcm_context gcm;
    bool use_gcm;
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
    else mbedtls_cipher_free(&ctx->cipher);
    if (ctx->key) mem_free(ctx->key);
    if (ctx->iv) mem_free(ctx->iv);
    if (ctx->aad) mem_free(ctx->aad);
    if (ctx->data) mem_free(ctx->data);
    mem_free(ctx);
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

extern "C" Item js_cipher_update(Item data_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__cipher_ctx__"));
    if (ctx_item.item == 0) return ItemNull;
    CipherCtx* ctx = (CipherCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx || ctx->finalized) return ItemNull;

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        cipher_ctx_append_data(ctx, (const uint8_t*)s->chars, (int)s->len);
    } else if (js_is_typed_array(data_item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(data_item, &buf, &len))
            cipher_ctx_append_data(ctx, buf, len);
    }

    // for non-GCM streaming, process complete blocks now
    if (!ctx->use_gcm && ctx->data_len > 0) {
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
            Item result = js_typed_array_new(JS_TYPED_UINT8, (int)olen);
            JsTypedArray* ta = (JsTypedArray*)result.map->data;
            if (ta && ta->data) memcpy(ta->data, out_buf, olen);
            mem_free(out_buf);
            return result;
        }
        mem_free(out_buf);
        return js_typed_array_new(JS_TYPED_UINT8, 0);
    }
    return js_typed_array_new(JS_TYPED_UINT8, 0);
}

extern "C" Item js_cipher_final(void) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__cipher_ctx__"));
    if (ctx_item.item == 0) return ItemNull;
    CipherCtx* ctx = (CipherCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx || ctx->finalized) return ItemNull;
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
            Item result = js_typed_array_new(JS_TYPED_UINT8, ctx->data_len);
            JsTypedArray* ta = (JsTypedArray*)result.map->data;
            if (ta && ta->data) memcpy(ta->data, out_buf, (size_t)ctx->data_len);
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
            Item result = js_typed_array_new(JS_TYPED_UINT8, ctx->data_len);
            JsTypedArray* ta = (JsTypedArray*)result.map->data;
            if (ta && ta->data) memcpy(ta->data, out_buf, (size_t)ctx->data_len);
            mem_free(out_buf);
            cipher_ctx_free(ctx);
            js_property_set(self, make_string_item_crypto("__cipher_ctx__"), ItemNull);
            return result;
        }
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
            Item result = js_typed_array_new(JS_TYPED_UINT8, (int)olen);
            JsTypedArray* ta = (JsTypedArray*)result.map->data;
            if (ta && ta->data) memcpy(ta->data, finish_buf, olen);
            return result;
        }
        return js_typed_array_new(JS_TYPED_UINT8, 0);
    }
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
        ret = mbedtls_cipher_set_iv(&ctx->cipher, iv, (size_t)iv_len);
        if (ret != 0) { cipher_ctx_free(ctx); return ItemNull; }
        ret = mbedtls_cipher_reset(&ctx->cipher);
        if (ret != 0) { cipher_ctx_free(ctx); return ItemNull; }
    }

    Item obj = js_new_object();
    js_property_set(obj, make_string_item_crypto("__cipher_ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    js_property_set(obj, make_string_item_crypto("update"),
                    js_new_function((void*)js_cipher_update, 1));
    js_property_set(obj, make_string_item_crypto("final"),
                    js_new_function((void*)js_cipher_final, 0));
    js_property_set(obj, make_string_item_crypto("getAuthTag"),
                    js_new_function((void*)js_cipher_getAuthTag, 0));
    js_property_set(obj, make_string_item_crypto("setAuthTag"),
                    js_new_function((void*)js_cipher_setAuthTag, 1));
    js_property_set(obj, make_string_item_crypto("setAAD"),
                    js_new_function((void*)js_cipher_setAAD, 1));
    return obj;
}

extern "C" Item js_crypto_createCipheriv(Item alg_item, Item key_item, Item iv_item) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) return ItemNull;
    String* alg = it2s(alg_item);
    char alg_buf[32];
    int alen = (int)alg->len < 31 ? (int)alg->len : 31;
    memcpy(alg_buf, alg->chars, (size_t)alen);
    alg_buf[alen] = '\0';

    uint8_t* key = NULL; int key_len = 0;
    uint8_t* iv = NULL; int iv_len = 0;
    if (!extract_bytes(key_item, &key, &key_len)) return ItemNull;
    if (!extract_bytes(iv_item, &iv, &iv_len)) { mem_free(key); return ItemNull; }

    return create_cipher_object(alg_buf, true, key, key_len, iv, iv_len);
}

extern "C" Item js_crypto_createDecipheriv(Item alg_item, Item key_item, Item iv_item) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) return ItemNull;
    String* alg = it2s(alg_item);
    char alg_buf[32];
    int alen = (int)alg->len < 31 ? (int)alg->len : 31;
    memcpy(alg_buf, alg->chars, (size_t)alen);
    alg_buf[alen] = '\0';

    uint8_t* key = NULL; int key_len = 0;
    uint8_t* iv = NULL; int iv_len = 0;
    if (!extract_bytes(key_item, &key, &key_len)) return ItemNull;
    if (!extract_bytes(iv_item, &iv, &iv_len)) { mem_free(key); return ItemNull; }

    return create_cipher_object(alg_buf, false, key, key_len, iv, iv_len);
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
    js_array_push(arr, make_string_item_crypto("aes-128-cbc"));
    js_array_push(arr, make_string_item_crypto("aes-192-cbc"));
    js_array_push(arr, make_string_item_crypto("aes-256-cbc"));
    js_array_push(arr, make_string_item_crypto("aes-128-ctr"));
    js_array_push(arr, make_string_item_crypto("aes-192-ctr"));
    js_array_push(arr, make_string_item_crypto("aes-256-ctr"));
    js_array_push(arr, make_string_item_crypto("aes-128-gcm"));
    js_array_push(arr, make_string_item_crypto("aes-192-gcm"));
    js_array_push(arr, make_string_item_crypto("aes-256-gcm"));
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
    {"aes-128-cbc", 419, 16, 16, 16, "cbc"},
    {"aes-192-cbc", 423, 16, 16, 24, "cbc"},
    {"aes-256-cbc", 427, 16, 16, 32, "cbc"},
    {"aes-128-ctr", 904,  1, 16, 16, "ctr"},
    {"aes-192-ctr", 905,  1, 16, 24, "ctr"},
    {"aes-256-ctr", 906,  1, 16, 32, "ctr"},
    {"aes-128-gcm", 895,  1, 12, 16, "gcm"},
    {"aes-192-gcm", 898,  1, 12, 24, "gcm"},
    {"aes-256-gcm", 901,  1, 12, 32, "gcm"},
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

extern "C" Item js_get_crypto_namespace(void) {
    if (crypto_namespace.item != 0) return crypto_namespace;

    crypto_namespace = js_new_object();

    crypto_set_method(crypto_namespace, "createHash",         (void*)js_crypto_createHash, 1);
    crypto_set_method(crypto_namespace, "hash",               (void*)js_crypto_hash, 3);
    crypto_set_method(crypto_namespace, "createHmac",         (void*)js_crypto_createHmac, 2);
    crypto_set_method(crypto_namespace, "createCipheriv",     (void*)js_crypto_createCipheriv, 3);
    crypto_set_method(crypto_namespace, "createDecipheriv",   (void*)js_crypto_createDecipheriv, 3);
    crypto_set_method(crypto_namespace, "argon2",             (void*)js_crypto_argon2_unsupported, 0);
    crypto_set_method(crypto_namespace, "argon2Sync",         (void*)js_crypto_argon2_unsupported, 0);
    crypto_set_method(crypto_namespace, "randomBytes",        (void*)js_crypto_randomBytes, 1);
    crypto_set_method(crypto_namespace, "randomFillSync",     (void*)js_crypto_randomFillSync, 3);
    crypto_set_method(crypto_namespace, "randomFill",         (void*)js_crypto_randomFill, 4);
    crypto_set_method(crypto_namespace, "randomUUID",         (void*)js_crypto_randomUUID, 1);
    crypto_set_method(crypto_namespace, "randomUUIDv7",       (void*)js_crypto_randomUUIDv7, 1);
    crypto_set_method(crypto_namespace, "randomInt",          (void*)js_crypto_randomInt, 2);
    crypto_set_method(crypto_namespace, "getFips",            (void*)js_crypto_getFips, 0);
    crypto_set_method(crypto_namespace, "getHashes",          (void*)js_crypto_getHashes, 0);
    crypto_set_method(crypto_namespace, "getCiphers",         (void*)js_crypto_getCiphers, 0);
    crypto_set_method(crypto_namespace, "getCipherInfo",      (void*)js_crypto_getCipherInfo, 2);
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
    js_property_set(crypto_namespace, make_string_item_crypto("Cipher"),
        js_new_function((void*)js_crypto_createCipheriv, 3));
    js_property_set(crypto_namespace, make_string_item_crypto("Decipher"),
        js_new_function((void*)js_crypto_createDecipheriv, 3));

    Item default_key = make_string_item_crypto("default");
    js_property_set(crypto_namespace, default_key, crypto_namespace);

    return crypto_namespace;
}

extern "C" void js_crypto_reset(void) {
    crypto_namespace = (Item){0};
}
