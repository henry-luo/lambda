/**
 * @file tls_handler.hpp
 * @brief TLS/SSL support via mbedTLS
 *
 * Provides certificate loading, TLS context management, and connection
 * wrapping for HTTPS support. Migrated from lib/serve/tls_handler.h.
 *
 * Compatible with:
 *   Express:   https.createServer({key, cert}, app)
 *   Flask:     app.run(ssl_context=('cert.pem', 'key.pem'))
 *   FastAPI:   uvicorn --ssl-keyfile key.pem --ssl-certfile cert.pem
 */

#pragma once

#include <uv.h>

// forward declare mbedTLS types to avoid header dependency
// actual mbedTLS headers included in .cpp
struct mbedtls_ssl_context;
struct mbedtls_ssl_config;
struct mbedtls_x509_crt;
struct mbedtls_pk_context;
struct mbedtls_entropy_context;
struct mbedtls_ctr_drbg_context;

// ============================================================================
// TLS Configuration
// ============================================================================

struct TlsConfig {
    const char *cert_file;          // path to PEM certificate file
    const char *key_file;           // path to PEM private key file
    const char *ca_file;            // path to CA certificate file (optional)
    const char *cipher_list;        // cipher suites (NULL for defaults)
    int verify_peer;                // verify client certificates (0/1)
    int min_version;                // minimum TLS version (0 = TLS 1.2)
};

TlsConfig tls_config_default(void);

// ============================================================================
// TLS Context (shared across connections)
// ============================================================================

struct TlsContext {
    mbedtls_ssl_config      *ssl_config;
    mbedtls_x509_crt        *own_cert;
    mbedtls_pk_context      *own_key;
    mbedtls_x509_crt        *ca_chain;
    mbedtls_entropy_context  *entropy;
    mbedtls_ctr_drbg_context *ctr_drbg;
    int                      initialized;
};

// create/destroy TLS context
TlsContext* tls_context_create(const TlsConfig *config);
void        tls_context_destroy(TlsContext *ctx);

// load certificates into an existing context
int tls_load_certificates(TlsContext *ctx, const char *cert_file, const char *key_file);
int tls_set_ca_certificates(TlsContext *ctx, const char *ca_file);

// ============================================================================
// TLS Connection (per-client)
// ============================================================================

struct TlsConnection {
    mbedtls_ssl_context *ssl;
    TlsContext          *ctx;       // shared context
    uv_tcp_t            *client;    // underlying TCP handle
    int                  handshake_done;
};

// create TLS connection wrapper for an accepted client
TlsConnection* tls_connection_create(TlsContext *ctx, uv_tcp_t *client);
void            tls_connection_destroy(TlsConnection *conn);

// perform TLS handshake (may need multiple calls for non-blocking I/O)
// returns 0 on success, positive if in progress, negative on error
int tls_handshake(TlsConnection *conn);

// read/write through TLS layer
int tls_read(TlsConnection *conn, unsigned char *buf, size_t len);
int tls_write(TlsConnection *conn, const unsigned char *buf, size_t len);

// ============================================================================
// Certificate info
// ============================================================================

// get peer certificate subject (returns malloc'd string or NULL)
char* tls_get_peer_subject(TlsConnection *conn);

// get negotiated cipher name
const char* tls_get_cipher_name(TlsConnection *conn);

// get negotiated protocol version string
const char* tls_get_protocol_version(TlsConnection *conn);

// ============================================================================
// Self-signed certificate generation (for development)
// ============================================================================

// generate a self-signed certificate and key, written to the given paths
// returns 0 on success
int tls_generate_self_signed(const char *cert_path, const char *key_path,
                             const char *common_name, int valid_days);

// ============================================================================
// Global init/cleanup
// ============================================================================

int  tls_init(void);
void tls_cleanup(void);
