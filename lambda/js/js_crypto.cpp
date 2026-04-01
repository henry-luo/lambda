/**
 * Native SHA-256/384/512 implementations for Lambda JS engine.
 * 
 * These replace the JavaScript Word64-based SHA implementations which are
 * extremely slow due to per-operation method call overhead through JS dispatch.
 * The native versions compute SHA directly on raw buffers and return Uint8Array.
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cstdlib>

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
    uint8_t* padded = (uint8_t*)calloc(padded_length, 1);
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
    free(padded);

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
    uint8_t* padded = (uint8_t*)calloc(padded_length, 1);
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
    free(padded);

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
