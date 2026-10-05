/**
 * @file trust_store.h
 * @brief The OS certificate trust store as one parsed mbedTLS chain.
 *
 * Loaded once per process, then shared read-only by every TLS client in the
 * host (the RDB bridge first; vibe/Lambda_IO_RDB.md section 13.15, Q5):
 *   - SSL_CERT_FILE, when set, replaces the platform store (OpenSSL convention);
 *   - macOS: Keychain trust settings of the system, admin and user domains,
 *     admin/user verdicts overriding system ones (a Deny removes a root);
 *   - Linux and BSDs: the distribution's CA bundle file;
 *   - Windows: the system "ROOT" certificate store.
 */

#ifndef LIB_TRUST_STORE_H
#define LIB_TRUST_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

struct mbedtls_x509_crt;

/**
 * The trusted roots, or NULL when none could be loaded (the reason is
 * logged). The chain is shared: callers must neither modify nor free it.
 */
struct mbedtls_x509_crt* trust_store_roots(void);

/** Number of roots in trust_store_roots(); 0 when none loaded. */
int trust_store_root_count(void);

#ifdef __cplusplus
}
#endif

#endif /* LIB_TRUST_STORE_H */
