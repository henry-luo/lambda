#include "signature.h"
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/provider.h>
#include <openssl/rsa.h>

static EVP_PKEY* signature_public_key(OSSL_LIB_CTX* context, SignatureScheme scheme, const uint8_t* key, size_t size) {
    if (scheme == SIGNATURE_ED25519)
        return size == 32 ? EVP_PKEY_new_raw_public_key_ex(context, "ED25519", "provider=default", key, size) : NULL;
    EVP_PKEY* result = NULL;
    static const char pem_header[] = "-----BEGIN PUBLIC KEY-----";
    if (size >= sizeof(pem_header) - 1 && !memcmp(key, pem_header, sizeof(pem_header) - 1)) {
        BIO* bio = BIO_new_mem_buf(key, (int)size);
        if (bio) {
            result = PEM_read_bio_PUBKEY_ex(bio, NULL, NULL, NULL, context, "provider=default");
            // Do not accept a second key or trailing non-whitespace material.
            char tail[128]; int length;
            while (result && (length = BIO_read(bio, tail, sizeof(tail))) > 0)
                for (int i = 0; i < length; i++) if (!strchr(" \t\r\n", tail[i]) || !tail[i]) {
                    EVP_PKEY_free(result); result = NULL; break;
                }
            BIO_free(bio);
        }
    } else {
        const unsigned char* cursor = key;
        result = d2i_PUBKEY_ex(NULL, &cursor, (long)size, context, "provider=default");
        if (result && cursor != key + size) { EVP_PKEY_free(result); result = NULL; }
    }
    if (!result) return NULL;
    bool valid = false;
    if (scheme == SIGNATURE_RSA_PSS_SHA256)
        valid = (EVP_PKEY_is_a(result, "RSA") || EVP_PKEY_is_a(result, "RSA-PSS")) &&
            EVP_PKEY_get_bits(result) >= 2048 && EVP_PKEY_get_bits(result) <= 8192;
    else if (scheme == SIGNATURE_ECDSA_P256_SHA256 && EVP_PKEY_is_a(result, "EC")) {
        char group[80]; size_t length = 0;
        valid = EVP_PKEY_get_utf8_string_param(result, OSSL_PKEY_PARAM_GROUP_NAME, group, sizeof(group), &length) == 1 &&
            (!strcmp(group, "prime256v1") || !strcmp(group, "secp256r1") || !strcmp(group, "P-256"));
    }
    if (!valid) { EVP_PKEY_free(result); return NULL; }
    return result;
}
SignatureResult signature_verify(SignatureScheme scheme, const uint8_t* key, size_t key_size,
        const uint8_t* message, size_t message_size, const uint8_t* signature, size_t signature_size) {
    static const uint8_t empty[1] = {0};
    if (!key || !key_size || key_size > 64 * 1024 || (!message && message_size) ||
            !signature || !signature_size || signature_size > 1024 ||
            scheme < SIGNATURE_ED25519 || scheme > SIGNATURE_ECDSA_P256_SHA256 ||
            (scheme == SIGNATURE_ED25519 && signature_size != 64)) return SIGNATURE_INVALID;
    // This profile differs from Node's general-purpose crypto API: a private
    // context and explicit built-in provider exclude ambient OpenSSL policy.
    if (OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, NULL) != 1) return SIGNATURE_ERROR;
    ERR_clear_error();
    OSSL_LIB_CTX* context = OSSL_LIB_CTX_new();
    OSSL_PROVIDER* provider = NULL; EVP_PKEY* public_key = NULL; EVP_MD_CTX* digest = NULL;
    SignatureResult result = SIGNATURE_ERROR;
    if (context && OSSL_PROVIDER_set_default_search_path(context, "") == 1)
        provider = OSSL_PROVIDER_load(context, "default");
    if (provider) {
        public_key = signature_public_key(context, scheme, key, key_size);
        if (!public_key) result = SIGNATURE_INVALID;
        else {
            digest = EVP_MD_CTX_new(); EVP_PKEY_CTX* key_context = NULL;
            const char* algorithm = scheme == SIGNATURE_ED25519 ? NULL : "SHA256";
            if (digest && EVP_DigestVerifyInit_ex(digest, &key_context, algorithm, context,
                    "provider=default", public_key, NULL) == 1) {
                bool configured = scheme != SIGNATURE_RSA_PSS_SHA256 ||
                    (EVP_PKEY_CTX_set_rsa_padding(key_context, RSA_PKCS1_PSS_PADDING) > 0 &&
                     EVP_PKEY_CTX_set_rsa_pss_saltlen(key_context, RSA_PSS_SALTLEN_DIGEST) > 0 &&
                     EVP_PKEY_CTX_set_rsa_mgf1_md_name(key_context, "SHA256", "provider=default") > 0);
                if (configured) result = EVP_DigestVerify(digest, signature, signature_size,
                    message ? message : empty, message_size) == 1 ? SIGNATURE_VALID : SIGNATURE_INVALID;
            }
        }
    }
    EVP_MD_CTX_free(digest); EVP_PKEY_free(public_key);
    if (provider) OSSL_PROVIDER_unload(provider);
    OSSL_LIB_CTX_free(context); ERR_clear_error();
    return result;
}
