#ifndef LIB_DIGEST_H
#define LIB_DIGEST_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DIGEST_MD5 = 128,
    DIGEST_SHA1 = 160,
    DIGEST_SHA224 = 224,
    DIGEST_SHA256 = 256,
    DIGEST_SHA384 = 384,
    DIGEST_SHA512 = 512
} DigestAlgorithm;

typedef struct DigestCtx DigestCtx;

size_t digest_output_len_bits(int bits);
bool digest_compute_bits(int bits, const void* data, size_t len,
                         uint8_t* out, size_t out_len);
bool digest_hmac_compute_bits(int bits, const void* key, size_t key_len,
                              const void* data, size_t len,
                              uint8_t* out, size_t out_len);
bool digest_pbkdf2_hmac_bits(int bits, const void* password, size_t password_len,
                             const void* salt, size_t salt_len,
                             unsigned int iterations,
                             uint8_t* out, size_t out_len);
bool digest_sha256(const void* data, size_t len, uint8_t out[32]);
bool digest_sha384(const void* data, size_t len, uint8_t out[48]);
bool digest_sha512(const void* data, size_t len, uint8_t out[64]);

DigestCtx* digest_ctx_new(int bits);
bool digest_update(DigestCtx* ctx, const void* data, size_t len);
bool digest_finalize(DigestCtx* ctx, uint8_t* out, size_t out_len);
void digest_ctx_free(DigestCtx* ctx);

#ifdef __cplusplus
}
#endif

#endif
