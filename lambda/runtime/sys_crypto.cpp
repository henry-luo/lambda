// sys_crypto.cpp — hash(value, algorithm) and uuid(), the digest and random
// identifier system functions planned as Archive AR-E7 and
// first needed by OAuth's PKCE (vibe/Lambda_Demo_Mail.md MG7, M4).
//
// Digests come from mbedTLS; uuid() draws on mbedTLS's entropy-seeded
// CTR_DRBG, the generator the rest of Lambda's TLS uses, so its value is
// unguessable (a PKCE verifier or an OAuth state may rest on it).

#include "io_file_search.hpp"
#include "../../lib/log.h"
#include "../../lib/uuid.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
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

// n unguessable bytes; false when the OS gives no entropy
static bool crypto_random(unsigned char* out, size_t n) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    static const char personal[] = "lambda uuid";
    bool ok = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                    (const unsigned char*)personal, sizeof(personal) - 1) == 0 &&
              mbedtls_ctr_drbg_random(&drbg, out, n) == 0;
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ok;
}

// uuid(): a random (version 4) UUID; a procedure, since each call differs
extern "C" Item pn_uuid(void) {
    uint8_t bytes[16];
    if (!crypto_random(bytes, sizeof(bytes))) {
        return io_fs_error(ERR_INTERNAL_ERROR, "uuid: no entropy source");
    }
    char text[UUID_STR_LEN];
    uuid_v4_format(bytes, text);
    return io_fs_make_string(text, UUID_STR_LEN - 1);
}
