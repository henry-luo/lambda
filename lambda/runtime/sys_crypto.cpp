// sys_crypto.cpp — hash(value, algorithm) and uuid(), the digest and random
// identifier system functions planned as Archive AR-E7 and
// first needed by OAuth's PKCE (vibe/Lambda_Demo_Mail.md MG7, M4).
//
// Digests come from mbedTLS; uuid() draws on mbedTLS's entropy-seeded
// CTR_DRBG, the generator the rest of Lambda's TLS uses, so its value is
// unguessable (a PKCE verifier or an OAuth state may rest on it).

#include "io_file_search.hpp"
#include "sys_crypto.hpp"
#include "lib/log.h"
#include "lib/uuid.h"
#include "lib/digest.h"
#include "lib/signature.h"

#include <mbedtls/md.h>
#include <mbedtls/gcm.h>
#include <mbedtls/platform_util.h>
#include <string.h>

static mbedtls_md_type_t hash_algorithm(Item algorithm) {
    if (!is_text_type_id(get_type_id(algorithm))) return MBEDTLS_MD_NONE;
    const char* name = algorithm.get_chars();
    size_t n = algorithm.get_len();
    if (n == 6 && !memcmp(name, "sha256", 6)) return MBEDTLS_MD_SHA256;
    if (n == 6 && !memcmp(name, "sha384", 6)) return MBEDTLS_MD_SHA384;
    if (n == 6 && !memcmp(name, "sha512", 6)) return MBEDTLS_MD_SHA512;
    return MBEDTLS_MD_NONE;
}

static bool signature_bytes(Item item, const uint8_t** bytes, size_t* size) {
    if (get_type_id(item) == LMD_TYPE_NULL) { *bytes = nullptr; *size = 0; return true; }
    if (get_type_id(item) == LMD_TYPE_BINARY) {
        Binary* binary = item.get_safe_binary(); *bytes = binary ? binary_data(binary) : nullptr;
        *size = binary ? binary_length(binary) : 0; return true;
    }
    if (is_text_type_id(get_type_id(item))) {
        *bytes = (const uint8_t*)item.get_chars(); *size = item.get_len(); return true;
    }
    return false;
}
extern "C" Item fn_verify_signature(Item key, Item message, Item signature, Item scheme) {
    Item arguments[] = {key, message, signature, scheme};
    for (Item argument : arguments) if (get_type_id(argument) == LMD_TYPE_ERROR) return argument;
    if (!is_text_type_id(get_type_id(scheme))) return io_fs_error(ERR_TYPE_MISMATCH, "verify_signature: scheme must be text");
    static const struct { const char* name; SignatureScheme scheme; } schemes[] = {
        {"ed25519", SIGNATURE_ED25519}, {"rsassa-pss-sha256", SIGNATURE_RSA_PSS_SHA256},
        {"ecdsa-sha2-nistp256", SIGNATURE_ECDSA_P256_SHA256}};
    const char* name = scheme.get_chars(); size_t length = scheme.get_len();
    SignatureScheme algorithm = SIGNATURE_ED25519; bool found = false;
    for (const auto& entry : schemes) if (length == strlen(entry.name) && !memcmp(name, entry.name, length)) {
        algorithm = entry.scheme; found = true; break;
    }
    if (!found) return io_fs_error(ERR_INVALID_OPERATION, "verify_signature: unsupported signature scheme");
    const uint8_t *key_bytes, *message_bytes, *signature_data; size_t key_size, message_size, signature_size;
    if (!signature_bytes(key, &key_bytes, &key_size) || !signature_bytes(message, &message_bytes, &message_size) ||
            !signature_bytes(signature, &signature_data, &signature_size))
        return io_fs_error(ERR_TYPE_MISMATCH, "verify_signature: key, message and signature must be bytes or text");
    SignatureResult result = signature_verify(algorithm, key_bytes, key_size, message_bytes, message_size, signature_data, signature_size);
    return result == SIGNATURE_ERROR ? io_fs_error(ERR_INTERNAL_ERROR, "verify_signature: cryptographic provider unavailable") :
        (Item){.item = b2it(result == SIGNATURE_VALID)};
}

// hash(value, algorithm): the digest of binary, or of text's UTF-8 bytes, as binary
extern "C" Item fn_hash(Item value, Item algorithm) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_ERROR) return value;
    mbedtls_md_type_t md = hash_algorithm(algorithm);
    if (md == MBEDTLS_MD_NONE) {
        return io_fs_error(ERR_INVALID_OPERATION, "hash: the algorithm must be 'sha256', 'sha384' or 'sha512'");
    }
    const unsigned char* bytes;
    size_t length;
    if (type == LMD_TYPE_BINARY) {
        Binary* binary = value.get_safe_binary();
        bytes = binary ? binary_data(binary) : (const unsigned char*)"";
        length = binary ? binary_length(binary) : 0;
    } else if (is_text_type_id(type)) {
        bytes = (const unsigned char*)value.get_chars();
        length = value.get_len();
    } else if (type == LMD_TYPE_NULL) {
        // the absent binary is the empty byte string (S2.2.2v2)
        bytes = (const unsigned char*)"";
        length = 0;
    } else {
        return io_fs_error(ERR_TYPE_MISMATCH, "hash: the value must be binary or text");
    }
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(md);
    unsigned char digest[MBEDTLS_MD_MAX_SIZE];
    if (!info || mbedtls_md(info, bytes, length, digest) != 0) {
        return io_fs_error(ERR_INTERNAL_ERROR, "hash: the digest failed");
    }
    Binary* result = heap_binary_from_bytes((const char*)digest, (int64_t)mbedtls_md_get_size(info));
    return result ? (Item){.item = x2it(result)} : ItemError;
}

// use the shared entropy provider so restricted builds fail closed too
bool crypto_random(unsigned char* out, size_t n) {
    return crypto_random_bytes(out, n);
}

// uuid(): a random (version 4) UUID; a procedure, since each call differs
extern "C" Item pn_uuid(void) {
    uint8_t bytes[16];
    if (!crypto_random_bytes(bytes, sizeof(bytes))) {
        return io_fs_error(ERR_INTERNAL_ERROR, "uuid: no entropy source");
    }
    char text[UUID_STR_LEN];
    uuid_v4_format(bytes, text);
    return io_fs_make_string(text, UUID_STR_LEN - 1);
}

// LGC1 || random 96-bit nonce || 128-bit tag || ciphertext. Context is GCM AAD.
static Item crypto_aead(Item key, Item input, Item context, bool encrypt) {
    Item args[] = {key, input, context};
    for (Item arg : args) if (get_type_id(arg) == LMD_TYPE_ERROR) return arg;
    const uint8_t *secret, *bytes, *aad; size_t secret_size, size, aad_size;
    if (get_type_id(key) != LMD_TYPE_BINARY || !signature_bytes(key, &secret, &secret_size) || secret_size != 32 ||
            !signature_bytes(input, &bytes, &size) || !signature_bytes(context, &aad, &aad_size) ||
            size > 8 * 1024 * 1024 + 32 || aad_size > 65536)
        return io_fs_error(ERR_INVALID_OPERATION, "io.crypto: expected a 32-byte key and bounded bytes/context");
    if (!encrypt && (size < 32 || memcmp(bytes, "LGC1", 4)))
        return io_fs_error(ERR_INVALID_OPERATION, "io.crypto: invalid ciphertext envelope");
    size_t plain_size = encrypt ? size : size - 32;
    if (plain_size > 8 * 1024 * 1024)
        return io_fs_error(ERR_INVALID_OPERATION, "io.crypto: payload limit");
    size_t output_size = encrypt ? size + 32 : plain_size;
    uint8_t* output = (uint8_t*)mem_calloc(output_size ? output_size : 1, 1, MEM_CAT_EVAL);
    if (!output) return ItemError;
    mbedtls_gcm_context cipher;
    mbedtls_gcm_init(&cipher);
    int status = mbedtls_gcm_setkey(&cipher, MBEDTLS_CIPHER_ID_AES, secret, 256);
    if (!status && encrypt) {
        memcpy(output, "LGC1", 4);
        if (!crypto_random(output + 4, 12)) status = -1;
        else status = mbedtls_gcm_crypt_and_tag(&cipher, MBEDTLS_GCM_ENCRYPT, size,
            output + 4, 12, aad, aad_size, bytes, output + 32, 16, output + 16);
    } else if (!status) {
        // authenticated decryption must verify before any plaintext is exposed.
        status = mbedtls_gcm_auth_decrypt(&cipher, plain_size, bytes + 4, 12, aad, aad_size,
            bytes + 16, 16, bytes + 32, output);
    }
    mbedtls_gcm_free(&cipher);
    Binary* result = status || !output_size ? nullptr : heap_binary_from_bytes((const char*)output, output_size);
    mbedtls_platform_zeroize(output, output_size);
    mem_free(output);
    return status ? io_fs_error(ERR_INVALID_OPERATION, "io.crypto: authentication or encryption failed") :
        !output_size ? ItemNull : result ? (Item){.item = x2it(result)} : ItemError;
}

extern "C" Item pn_io_crypto_seal(Item key, Item bytes, Item context) {
    return crypto_aead(key, bytes, context, true);
}
extern "C" Item fn_io_crypto_open(Item key, Item bytes, Item context) {
    return crypto_aead(key, bytes, context, false);
}
extern "C" Item fn_io_crypto_mac(Item key, Item input) {
    if (get_type_id(key) == LMD_TYPE_ERROR) return key;
    if (get_type_id(input) == LMD_TYPE_ERROR) return input;
    const uint8_t *secret, *bytes; size_t secret_size, size;
    if (get_type_id(key) != LMD_TYPE_BINARY || !signature_bytes(key, &secret, &secret_size) || secret_size != 32 ||
            !signature_bytes(input, &bytes, &size) || size > 8 * 1024 * 1024)
        return io_fs_error(ERR_INVALID_OPERATION, "io.crypto.mac: expected a 32-byte key and bounded bytes");
    uint8_t digest[32];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), secret, secret_size, bytes, size, digest))
        return io_fs_error(ERR_INTERNAL_ERROR, "io.crypto.mac: provider failure");
    Binary* result = heap_binary_from_bytes((const char*)digest, sizeof(digest));
    mbedtls_platform_zeroize(digest, sizeof(digest));
    return result ? (Item){.item = x2it(result)} : ItemError;
}
extern "C" Item pn_io_crypto_key(void) {
    uint8_t secret[32];
    if (!crypto_random(secret, sizeof(secret))) return io_fs_error(ERR_INTERNAL_ERROR, "io.crypto.key: no entropy source");
    Binary* result = heap_binary_from_bytes((const char*)secret, sizeof(secret));
    mbedtls_platform_zeroize(secret, sizeof(secret));
    return result ? (Item){.item = x2it(result)} : ItemError;
}
