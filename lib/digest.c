#include "digest.h"

#include "log.h"
#include "mem.h"

#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>

struct DigestCtx {
    mbedtls_md_context_t md_ctx;
    const mbedtls_md_info_t* info;
    bool active;
};

static const uint8_t DIGEST_EMPTY_BYTES[1] = {0};

static mbedtls_md_type_t digest_md_type_for_bits(int bits) {
    switch (bits) {
        case DIGEST_MD5: return MBEDTLS_MD_MD5;
        case DIGEST_SHA1: return MBEDTLS_MD_SHA1;
        case DIGEST_SHA224: return MBEDTLS_MD_SHA224;
        case DIGEST_SHA256: return MBEDTLS_MD_SHA256;
        case DIGEST_SHA384: return MBEDTLS_MD_SHA384;
        case DIGEST_SHA512: return MBEDTLS_MD_SHA512;
        default: return MBEDTLS_MD_NONE;
    }
}

static const mbedtls_md_info_t* digest_info_for_bits(int bits) {
    mbedtls_md_type_t md_type = digest_md_type_for_bits(bits);
    if (md_type == MBEDTLS_MD_NONE) return NULL;
    return mbedtls_md_info_from_type(md_type);
}

size_t digest_output_len_bits(int bits) {
    const mbedtls_md_info_t* info = digest_info_for_bits(bits);
    return info ? (size_t)mbedtls_md_get_size(info) : 0;
}

static bool digest_input_bytes(const void* data, size_t len, const uint8_t** bytes) {
    if (data) {
        *bytes = (const uint8_t*)data;
        return true;
    }
    if (len == 0) {
        *bytes = DIGEST_EMPTY_BYTES;
        return true;
    }
    return false;
}

bool digest_compute_bits(int bits, const void* data, size_t len,
                         uint8_t* out, size_t out_len) {
    const mbedtls_md_info_t* info = digest_info_for_bits(bits);
    size_t digest_len = info ? (size_t)mbedtls_md_get_size(info) : 0;
    if (!info || !out || out_len < digest_len) return false;

    const uint8_t* bytes = NULL;
    if (!digest_input_bytes(data, len, &bytes)) return false;
    int ret = mbedtls_md(info, bytes, len, out);
    if (ret != 0) {
        log_error("digest: one-shot digest failed bits=%d ret=%d", bits, ret);
        return false;
    }
    return true;
}

bool digest_hmac_compute_bits(int bits, const void* key, size_t key_len,
                              const void* data, size_t len,
                              uint8_t* out, size_t out_len) {
    const mbedtls_md_info_t* info = digest_info_for_bits(bits);
    size_t digest_len = info ? (size_t)mbedtls_md_get_size(info) : 0;
    if (!info || !out || out_len < digest_len) return false;

    const uint8_t* key_bytes = NULL;
    const uint8_t* data_bytes = NULL;
    if (!digest_input_bytes(key, key_len, &key_bytes) ||
        !digest_input_bytes(data, len, &data_bytes)) {
        return false;
    }

    int ret = mbedtls_md_hmac(info, key_bytes, key_len, data_bytes, len, out);
    if (ret != 0) {
        log_error("digest: hmac failed bits=%d ret=%d", bits, ret);
        return false;
    }
    return true;
}

bool digest_pbkdf2_hmac_bits(int bits, const void* password, size_t password_len,
                             const void* salt, size_t salt_len,
                             unsigned int iterations,
                             uint8_t* out, size_t out_len) {
    mbedtls_md_type_t md_type = digest_md_type_for_bits(bits);
    if (md_type == MBEDTLS_MD_NONE || iterations == 0 || !out || out_len == 0) {
        return false;
    }

    const uint8_t* password_bytes = NULL;
    const uint8_t* salt_bytes = NULL;
    if (!digest_input_bytes(password, password_len, &password_bytes) ||
        !digest_input_bytes(salt, salt_len, &salt_bytes)) {
        return false;
    }

    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(md_type, password_bytes, password_len,
                                            salt_bytes, salt_len, iterations,
                                            (uint32_t)out_len, out);
    if (ret != 0) {
        log_error("digest: pbkdf2-hmac failed bits=%d ret=%d", bits, ret);
        return false;
    }
    return true;
}

bool digest_sha256(const void* data, size_t len, uint8_t out[32]) {
    return digest_compute_bits(DIGEST_SHA256, data, len, out, 32);
}

bool digest_sha384(const void* data, size_t len, uint8_t out[48]) {
    return digest_compute_bits(DIGEST_SHA384, data, len, out, 48);
}

bool digest_sha512(const void* data, size_t len, uint8_t out[64]) {
    return digest_compute_bits(DIGEST_SHA512, data, len, out, 64);
}

DigestCtx* digest_ctx_new(int bits) {
    const mbedtls_md_info_t* info = digest_info_for_bits(bits);
    if (!info) return NULL;

    DigestCtx* ctx = (DigestCtx*)mem_calloc(1, sizeof(DigestCtx), MEM_CAT_SYSTEM);
    if (!ctx) return NULL;

    mbedtls_md_init(&ctx->md_ctx);
    int ret = mbedtls_md_setup(&ctx->md_ctx, info, 0);
    if (ret == 0) ret = mbedtls_md_starts(&ctx->md_ctx);
    if (ret != 0) {
        log_error("digest: context init failed bits=%d ret=%d", bits, ret);
        mbedtls_md_free(&ctx->md_ctx);
        mem_free(ctx);
        return NULL;
    }

    ctx->info = info;
    ctx->active = true;
    return ctx;
}

bool digest_update(DigestCtx* ctx, const void* data, size_t len) {
    if (!ctx || !ctx->active) return false;
    if (len == 0) return true;

    const uint8_t* bytes = NULL;
    if (!digest_input_bytes(data, len, &bytes)) return false;
    int ret = mbedtls_md_update(&ctx->md_ctx, bytes, len);
    if (ret != 0) {
        log_error("digest: context update failed ret=%d", ret);
        return false;
    }
    return true;
}

bool digest_finalize(DigestCtx* ctx, uint8_t* out, size_t out_len) {
    if (!ctx || !ctx->active || !ctx->info || !out) return false;
    size_t digest_len = (size_t)mbedtls_md_get_size(ctx->info);
    if (out_len < digest_len) return false;

    int ret = mbedtls_md_finish(&ctx->md_ctx, out);
    if (ret != 0) {
        log_error("digest: context finish failed ret=%d", ret);
        return false;
    }
    ctx->active = false;
    return true;
}

void digest_ctx_free(DigestCtx* ctx) {
    if (!ctx) return;
    mbedtls_md_free(&ctx->md_ctx);
    mem_free(ctx);
}
