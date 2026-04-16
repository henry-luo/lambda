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
#include "../lambda-data.hpp"

extern "C" Item js_get_current_this(void);
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include <cstring>
#include "../../lib/mem.h"

#ifdef _WIN32
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// ============================================================================
// SHA-256 constants
// ============================================================================
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// ============================================================================
// SHA-512 constants
// ============================================================================
static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

// SHA-512 initial hash values
static const uint64_t sha512_h0[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

// SHA-384 initial hash values
static const uint64_t sha384_h0[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL,
    0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
    0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL
};

// ============================================================================
// SHA-256 implementation
// ============================================================================

static inline uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_compute(const uint8_t* data, int offset, int length, uint8_t* out) {
    uint32_t h0 = 0x6a09e667, h1 = 0xbb67ae85, h2 = 0x3c6ef372, h3 = 0xa54ff53a;
    uint32_t h4 = 0x510e527f, h5 = 0x9b05688c, h6 = 0x1f83d9ab, h7 = 0x5be0cd19;

    int padded_length = ((length + 9 + 63) / 64) * 64;
    uint8_t* padded = (uint8_t*)mem_calloc(padded_length, 1, MEM_CAT_JS_RUNTIME);
    memcpy(padded, data + offset, length);
    padded[length] = 0x80;

    // length in bits as big-endian 64-bit at end
    uint64_t bit_len = (uint64_t)length * 8;
    for (int i = 0; i < 8; i++) {
        padded[padded_length - 1 - i] = (uint8_t)(bit_len >> (i * 8));
    }

    uint32_t w[64];
    for (int i = 0; i < padded_length; i += 64) {
        for (int j = 0; j < 16; j++) {
            w[j] = ((uint32_t)padded[i + j*4] << 24) |
                    ((uint32_t)padded[i + j*4 + 1] << 16) |
                    ((uint32_t)padded[i + j*4 + 2] << 8) |
                    ((uint32_t)padded[i + j*4 + 3]);
        }
        for (int j = 16; j < 64; j++) {
            uint32_t s0 = rotr32(w[j-15], 7) ^ rotr32(w[j-15], 18) ^ (w[j-15] >> 3);
            uint32_t s1 = rotr32(w[j-2], 17) ^ rotr32(w[j-2], 19) ^ (w[j-2] >> 10);
            w[j] = w[j-16] + s0 + w[j-7] + s1;
        }

        uint32_t a = h0, b = h1, c = h2, d = h3;
        uint32_t e = h4, f = h5, g = h6, h = h7;

        for (int j = 0; j < 64; j++) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + sha256_k[j] + w[j];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h0 += a; h1 += b; h2 += c; h3 += d;
        h4 += e; h5 += f; h6 += g; h7 += h;
    }
    mem_free(padded);

    uint32_t hh[8] = { h0, h1, h2, h3, h4, h5, h6, h7 };
    for (int i = 0; i < 8; i++) {
        out[i*4]     = (uint8_t)(hh[i] >> 24);
        out[i*4 + 1] = (uint8_t)(hh[i] >> 16);
        out[i*4 + 2] = (uint8_t)(hh[i] >> 8);
        out[i*4 + 3] = (uint8_t)(hh[i]);
    }
}

// ============================================================================
// SHA-512 implementation (also used for SHA-384)
// ============================================================================

static inline uint64_t rotr64(uint64_t x, int n) {
    return (x >> n) | (x << (64 - n));
}

static void sha512_compute(const uint8_t* data, int offset, int length, bool mode384, uint8_t* out) {
    uint64_t h[8];
    if (mode384) {
        memcpy(h, sha384_h0, sizeof(h));
    } else {
        memcpy(h, sha512_h0, sizeof(h));
    }

    int padded_length = ((length + 17 + 127) / 128) * 128;
    uint8_t* padded = (uint8_t*)mem_calloc(padded_length, 1, MEM_CAT_JS_RUNTIME);
    memcpy(padded, data + offset, length);
    padded[length] = 0x80;

    // length in bits as big-endian 128-bit at end (we only use lower 64 bits)
    uint64_t bit_len = (uint64_t)length * 8;
    for (int i = 0; i < 8; i++) {
        padded[padded_length - 1 - i] = (uint8_t)(bit_len >> (i * 8));
    }

    uint64_t w[80];
    for (int i = 0; i < padded_length; i += 128) {
        for (int j = 0; j < 16; j++) {
            int base = i + j * 8;
            w[j] = ((uint64_t)padded[base] << 56) |
                    ((uint64_t)padded[base+1] << 48) |
                    ((uint64_t)padded[base+2] << 40) |
                    ((uint64_t)padded[base+3] << 32) |
                    ((uint64_t)padded[base+4] << 24) |
                    ((uint64_t)padded[base+5] << 16) |
                    ((uint64_t)padded[base+6] << 8) |
                    ((uint64_t)padded[base+7]);
        }
        for (int j = 16; j < 80; j++) {
            uint64_t s0 = rotr64(w[j-15], 1) ^ rotr64(w[j-15], 8) ^ (w[j-15] >> 7);
            uint64_t s1 = rotr64(w[j-2], 19) ^ rotr64(w[j-2], 61) ^ (w[j-2] >> 6);
            w[j] = w[j-16] + s0 + w[j-7] + s1;
        }

        uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int j = 0; j < 80; j++) {
            uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            uint64_t ch = (e & f) ^ (~e & g);
            uint64_t t1 = hh + S1 + ch + sha512_k[j] + w[j];
            uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint64_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    mem_free(padded);

    int out_words = mode384 ? 6 : 8;
    for (int i = 0; i < out_words; i++) {
        out[i*8]     = (uint8_t)(h[i] >> 56);
        out[i*8 + 1] = (uint8_t)(h[i] >> 48);
        out[i*8 + 2] = (uint8_t)(h[i] >> 40);
        out[i*8 + 3] = (uint8_t)(h[i] >> 32);
        out[i*8 + 4] = (uint8_t)(h[i] >> 24);
        out[i*8 + 5] = (uint8_t)(h[i] >> 16);
        out[i*8 + 6] = (uint8_t)(h[i] >> 8);
        out[i*8 + 7] = (uint8_t)(h[i]);
    }
}

// ============================================================================
// Helper: extract uint8 buffer from a JS typed array Item
// ============================================================================

static bool get_uint8_buffer(Item ta_item, const uint8_t** out_data, int* out_length) {
    if (!js_is_typed_array(ta_item)) return false;
    JsTypedArray* ta = (JsTypedArray*)ta_item.map->data;
    if (!ta || !ta->data) return false;
    *out_data = (const uint8_t*)ta->data;
    *out_length = ta->length;
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

extern "C" Item js_crypto_randomBytes(Item size_item) {
    int size = (int)it2i(size_item);
    if (size <= 0 || size > 65536) {
        log_error("crypto: randomBytes: invalid size %d", size);
        return ItemNull;
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

// ============================================================================
// randomUUID() → string "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
// ============================================================================

extern "C" Item js_crypto_randomUUID(void) {
    uint8_t bytes[16];
    if (!crypto_random_bytes(bytes, 16)) {
        log_error("crypto: randomUUID: entropy source failed");
        return ItemNull;
    }
    // set version 4 and variant 1
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    static const char hex[] = "0123456789abcdef";
    char uuid[37];
    int p = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) uuid[p++] = '-';
        uuid[p++] = hex[bytes[i] >> 4];
        uuid[p++] = hex[bytes[i] & 0x0F];
    }
    uuid[p] = '\0';
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

// ============================================================================
// HMAC — hash-based message authentication code (native implementation)
// ============================================================================

static void hmac_compute(const uint8_t* key, int key_len,
                         const uint8_t* data, int data_len,
                         const char* alg, uint8_t* out, int* out_len) {
    int block_size;
    int hash_len;
    void (*hash_fn)(const uint8_t*, int, int, uint8_t*) = NULL;
    void (*hash_fn512)(const uint8_t*, int, int, bool, uint8_t*) = NULL;
    bool is384 = false;

    if (strcmp(alg, "sha256") == 0) {
        block_size = 64; hash_len = 32;
        hash_fn = sha256_compute;
    } else if (strcmp(alg, "sha384") == 0) {
        block_size = 128; hash_len = 48;
        hash_fn512 = sha512_compute; is384 = true;
    } else if (strcmp(alg, "sha512") == 0) {
        block_size = 128; hash_len = 64;
        hash_fn512 = sha512_compute; is384 = false;
    } else {
        *out_len = 0;
        return;
    }

    // derive K'
    uint8_t k_prime[128];
    memset(k_prime, 0, (size_t)block_size);
    if (key_len > block_size) {
        if (hash_fn) hash_fn(key, 0, key_len, k_prime);
        else hash_fn512(key, 0, key_len, is384, k_prime);
    } else {
        memcpy(k_prime, key, (size_t)key_len);
    }

    // inner hash
    int inner_len = block_size + data_len;
    uint8_t* inner = (uint8_t*)mem_alloc((size_t)inner_len, MEM_CAT_JS_RUNTIME);
    for (int i = 0; i < block_size; i++) inner[i] = k_prime[i] ^ 0x36;
    if (data_len > 0) memcpy(inner + block_size, data, (size_t)data_len);

    uint8_t inner_hash[64];
    if (hash_fn) hash_fn(inner, 0, inner_len, inner_hash);
    else hash_fn512(inner, 0, inner_len, is384, inner_hash);
    mem_free(inner);

    // outer hash
    int outer_len = block_size + hash_len;
    uint8_t* outer = (uint8_t*)mem_alloc((size_t)outer_len, MEM_CAT_JS_RUNTIME);
    for (int i = 0; i < block_size; i++) outer[i] = k_prime[i] ^ 0x5c;
    memcpy(outer + block_size, inner_hash, (size_t)hash_len);

    if (hash_fn) hash_fn(outer, 0, outer_len, out);
    else hash_fn512(outer, 0, outer_len, is384, out);
    mem_free(outer);
    *out_len = hash_len;
}

// ============================================================================
// Encoding helpers
// ============================================================================

static const char hex_chars[] = "0123456789abcdef";

static Item bytes_to_hex_string(const uint8_t* bytes, int len) {
    char* hex = (char*)mem_alloc((size_t)(len * 2 + 1), MEM_CAT_JS_RUNTIME);
    for (int i = 0; i < len; i++) {
        hex[i*2] = hex_chars[bytes[i] >> 4];
        hex[i*2+1] = hex_chars[bytes[i] & 0x0F];
    }
    hex[len*2] = '\0';
    Item result = make_string_item_crypto(hex);
    mem_free(hex);
    return result;
}

static Item bytes_to_base64_string(const uint8_t* bytes, int len) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int out_len = ((len + 2) / 3) * 4;
    char* out = (char*)mem_alloc((size_t)(out_len + 1), MEM_CAT_JS_RUNTIME);
    int j = 0;
    for (int i = 0; i < len; i += 3) {
        uint32_t n = ((uint32_t)bytes[i]) << 16;
        if (i + 1 < len) n |= ((uint32_t)bytes[i+1]) << 8;
        if (i + 2 < len) n |= bytes[i+2];
        out[j++] = b64[(n >> 18) & 0x3F];
        out[j++] = b64[(n >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? b64[n & 0x3F] : '=';
    }
    out[j] = '\0';
    Item result = make_string_item_crypto(out);
    mem_free(out);
    return result;
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

extern "C" Item js_hmac_update(Item data_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__hmac_ctx__"));
    if (ctx_item.item == 0) return self;
    HmacCtx* ctx = (HmacCtx*)(uintptr_t)it2i(ctx_item);
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

extern "C" Item js_hmac_digest(Item encoding_item) {
    Item self = js_get_current_this();
    Item ctx_item = js_property_get(self, make_string_item_crypto("__hmac_ctx__"));
    if (ctx_item.item == 0) return ItemNull;
    HmacCtx* ctx = (HmacCtx*)(uintptr_t)it2i(ctx_item);
    if (!ctx) return ItemNull;

    uint8_t hash[64];
    int hash_len = 0;
    hmac_compute(ctx->key, ctx->key_len, ctx->data, ctx->data_len, ctx->alg, hash, &hash_len);

    if (ctx->key) mem_free(ctx->key);
    if (ctx->data) mem_free(ctx->data);
    mem_free(ctx);
    js_property_set(self, make_string_item_crypto("__hmac_ctx__"), ItemNull);

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

extern "C" Item js_crypto_createHmac(Item alg_item, Item key_item) {
    if (get_type_id(alg_item) != LMD_TYPE_STRING) return ItemNull;
    String* alg = it2s(alg_item);

    HmacCtx* ctx = (HmacCtx*)mem_calloc(1, sizeof(HmacCtx), MEM_CAT_JS_RUNTIME);
    int alen = (int)alg->len < 15 ? (int)alg->len : 15;
    memcpy(ctx->alg, alg->chars, (size_t)alen);
    ctx->alg[alen] = '\0';

    if (get_type_id(key_item) == LMD_TYPE_STRING) {
        String* ks = it2s(key_item);
        ctx->key = (uint8_t*)mem_alloc(ks->len, MEM_CAT_JS_RUNTIME);
        memcpy(ctx->key, ks->chars, ks->len);
        ctx->key_len = (int)ks->len;
    } else if (js_is_typed_array(key_item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(key_item, &buf, &len)) {
            ctx->key = (uint8_t*)mem_alloc((size_t)len, MEM_CAT_JS_RUNTIME);
            memcpy(ctx->key, buf, (size_t)len);
            ctx->key_len = len;
        }
    }

    Item obj = js_new_object();
    js_property_set(obj, make_string_item_crypto("__hmac_ctx__"),
                    (Item){.item = i2it((int64_t)(uintptr_t)ctx)});
    js_property_set(obj, make_string_item_crypto("update"),
                    js_new_function((void*)js_hmac_update, 1));
    js_property_set(obj, make_string_item_crypto("digest"),
                    js_new_function((void*)js_hmac_digest, 1));
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

    if (strcmp(ctx->alg, "sha256") == 0) {
        sha256_compute(ctx->data ? ctx->data : (const uint8_t*)"", 0, ctx->data_len, hash);
        hash_len = 32;
    } else if (strcmp(ctx->alg, "sha384") == 0) {
        sha512_compute(ctx->data ? ctx->data : (const uint8_t*)"", 0, ctx->data_len, true, hash);
        hash_len = 48;
    } else if (strcmp(ctx->alg, "sha512") == 0) {
        sha512_compute(ctx->data ? ctx->data : (const uint8_t*)"", 0, ctx->data_len, false, hash);
        hash_len = 64;
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
    if (get_type_id(alg_item) != LMD_TYPE_STRING) return ItemNull;
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

// ============================================================================
// getHashes() → array of supported algorithm names
// ============================================================================

extern "C" Item js_crypto_getHashes(void) {
    Item arr = js_array_new(3);
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
    if (get_type_id(item) == LMD_TYPE_STRING) {
        String* s = it2s(item);
        *out = (uint8_t*)mem_alloc(s->len, MEM_CAT_JS_RUNTIME);
        memcpy(*out, s->chars, s->len);
        *out_len = (int)s->len;
        return true;
    } else if (js_is_typed_array(item)) {
        const uint8_t* buf; int len;
        if (get_uint8_buffer(item, &buf, &len)) {
            *out = (uint8_t*)mem_alloc((size_t)len, MEM_CAT_JS_RUNTIME);
            memcpy(*out, buf, (size_t)len);
            *out_len = len;
            return true;
        }
    }
    return false;
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

#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>

static mbedtls_md_type_t resolve_md_type(const char* digest) {
    if (strcmp(digest, "sha1") == 0) return MBEDTLS_MD_SHA1;
    if (strcmp(digest, "sha256") == 0) return MBEDTLS_MD_SHA256;
    if (strcmp(digest, "sha384") == 0) return MBEDTLS_MD_SHA384;
    if (strcmp(digest, "sha512") == 0) return MBEDTLS_MD_SHA512;
    if (strcmp(digest, "sha224") == 0) return MBEDTLS_MD_SHA224;
    return MBEDTLS_MD_NONE;
}

extern "C" Item js_crypto_pbkdf2Sync(Item pass_item, Item salt_item, Item iter_item,
                                      Item keylen_item, Item digest_item) {
    uint8_t* pass = NULL; int pass_len = 0;
    uint8_t* salt = NULL; int salt_len = 0;

    if (!extract_bytes(pass_item, &pass, &pass_len)) return ItemNull;
    if (!extract_bytes(salt_item, &salt, &salt_len)) { mem_free(pass); return ItemNull; }

    int iterations = (int)it2i(iter_item);
    int keylen = (int)it2i(keylen_item);

    if (iterations < 1 || keylen < 1 || keylen > 1024) {
        log_error("crypto: pbkdf2Sync: invalid iterations=%d keylen=%d", iterations, keylen);
        mem_free(pass); mem_free(salt);
        return ItemNull;
    }

    char digest_buf[32] = "sha1";
    if (get_type_id(digest_item) == LMD_TYPE_STRING) {
        String* d = it2s(digest_item);
        int dlen = (int)d->len < 31 ? (int)d->len : 31;
        memcpy(digest_buf, d->chars, (size_t)dlen);
        digest_buf[dlen] = '\0';
    }

    mbedtls_md_type_t md = resolve_md_type(digest_buf);
    if (md == MBEDTLS_MD_NONE) {
        log_error("crypto: pbkdf2Sync: unsupported digest: %s", digest_buf);
        mem_free(pass); mem_free(salt);
        return ItemNull;
    }

    uint8_t* output = (uint8_t*)mem_alloc((size_t)keylen, MEM_CAT_JS_RUNTIME);
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(md, pass, (size_t)pass_len,
                salt, (size_t)salt_len, (unsigned int)iterations, (uint32_t)keylen, output);
    mem_free(pass);
    mem_free(salt);

    if (ret != 0) {
        log_error("crypto: pbkdf2Sync failed: %d", ret);
        mem_free(output);
        return ItemNull;
    }

    Item result = js_typed_array_new(JS_TYPED_UINT8, keylen);
    JsTypedArray* ta = (JsTypedArray*)result.map->data;
    if (ta && ta->data) memcpy(ta->data, output, (size_t)keylen);
    mem_free(output);
    return result;
}

// async variant calls callback with (err, derivedKey)
extern "C" Item js_crypto_pbkdf2(Item pass_item, Item salt_item, Item iter_item,
                                  Item keylen_item, Item digest_item, Item callback_item) {
    // for simplicity, run synchronously and invoke callback
    Item derived = js_crypto_pbkdf2Sync(pass_item, salt_item, iter_item, keylen_item, digest_item);
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

    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, pass, (size_t)pass_len,
                salt, (size_t)salt_len, 1, (uint32_t)B_len, B);
    if (ret != 0) {
        log_error("crypto: scryptSync: initial PBKDF2 failed: %d", ret);
        mem_free(pass); mem_free(salt); mem_free(B);
        return ItemNull;
    }

    // step 2: ROMix each block
    for (int i = 0; i < p; i++) {
        scrypt_romix((uint32_t*)(B + i * block_size), r, N);
    }

    // step 3: PBKDF2-HMAC-SHA256 with B as salt to derive final key
    uint8_t* output = (uint8_t*)mem_alloc((size_t)keylen, MEM_CAT_JS_RUNTIME);
    ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, pass, (size_t)pass_len,
                B, (size_t)B_len, 1, (uint32_t)keylen, output);
    mem_free(pass);
    mem_free(salt);
    mem_free(B);

    if (ret != 0) {
        log_error("crypto: scryptSync: final PBKDF2 failed: %d", ret);
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
    Item arr = js_array_new(9);
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
    if (strcmp(normalized, "sha256") == 0) {
        sha256_compute(buf, 0, buf_len, hash);
        hash_len = 32;
    } else if (strcmp(normalized, "sha384") == 0) {
        sha512_compute(buf, 0, buf_len, true, hash);
        hash_len = 48;
    } else if (strcmp(normalized, "sha512") == 0) {
        sha512_compute(buf, 0, buf_len, false, hash);
        hash_len = 64;
    } else if (strcmp(normalized, "sha1") == 0) {
        // use mbedTLS for SHA-1
        mbedtls_md_type_t md = MBEDTLS_MD_SHA1;
        const mbedtls_md_info_t* info = mbedtls_md_info_from_type(md);
        if (info) {
            mbedtls_md(info, buf, (size_t)buf_len, hash);
            hash_len = 20;
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

static Item crypto_namespace = {0};

static void crypto_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item_crypto(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_crypto_namespace(void) {
    if (crypto_namespace.item != 0) return crypto_namespace;

    crypto_namespace = js_new_object();

    crypto_set_method(crypto_namespace, "createHash",         (void*)js_crypto_createHash, 1);
    crypto_set_method(crypto_namespace, "createHmac",         (void*)js_crypto_createHmac, 2);
    crypto_set_method(crypto_namespace, "createCipheriv",     (void*)js_crypto_createCipheriv, 3);
    crypto_set_method(crypto_namespace, "createDecipheriv",   (void*)js_crypto_createDecipheriv, 3);
    crypto_set_method(crypto_namespace, "randomBytes",        (void*)js_crypto_randomBytes, 1);
    crypto_set_method(crypto_namespace, "randomUUID",         (void*)js_crypto_randomUUID, 0);
    crypto_set_method(crypto_namespace, "randomInt",          (void*)js_crypto_randomInt, 2);
    crypto_set_method(crypto_namespace, "getHashes",          (void*)js_crypto_getHashes, 0);
    crypto_set_method(crypto_namespace, "getCiphers",         (void*)js_crypto_getCiphers, 0);
    crypto_set_method(crypto_namespace, "timingSafeEqual",    (void*)js_crypto_timingSafeEqual, 2);
    crypto_set_method(crypto_namespace, "pbkdf2Sync",         (void*)js_crypto_pbkdf2Sync, 5);
    crypto_set_method(crypto_namespace, "pbkdf2",             (void*)js_crypto_pbkdf2, 6);
    crypto_set_method(crypto_namespace, "scryptSync",         (void*)js_crypto_scryptSync, 4);

    // subtle Web Crypto API (subset)
    Item subtle = js_new_object();
    crypto_set_method(subtle, "digest",  (void*)js_subtle_digest, 2);
    crypto_set_method(subtle, "encrypt", (void*)js_subtle_encrypt, 3);
    crypto_set_method(subtle, "decrypt", (void*)js_subtle_decrypt, 3);
    js_property_set(crypto_namespace, make_string_item_crypto("subtle"), subtle);

    Item default_key = make_string_item_crypto("default");
    js_property_set(crypto_namespace, default_key, crypto_namespace);

    return crypto_namespace;
}

extern "C" void js_crypto_reset(void) {
    crypto_namespace = (Item){0};
}
