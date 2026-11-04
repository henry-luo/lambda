#pragma once
#include "lambda_api.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum SignatureScheme {
    SIGNATURE_ED25519, SIGNATURE_RSA_PSS_SHA256, SIGNATURE_ECDSA_P256_SHA256
} SignatureScheme;
typedef enum SignatureResult { SIGNATURE_VALID = 1, SIGNATURE_INVALID = 0, SIGNATURE_ERROR = -1 } SignatureResult;
// Public keys only: raw 32-byte Ed25519 or PEM/DER SubjectPublicKeyInfo for
// RSA/P-256. Verification uses an isolated built-in provider context, never
// ambient configuration, key files, engines, modules or credentials.
LAMBDA_LIB_API SignatureResult signature_verify(SignatureScheme scheme,
    const uint8_t* key, size_t key_size, const uint8_t* message, size_t message_size,
    const uint8_t* signature, size_t signature_size);
#ifdef __cplusplus
}
#endif
