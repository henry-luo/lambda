/**
 * @file tls_handler.cpp
 * @brief TLS/SSL implementation using mbedTLS
 *
 * Migrated from lib/serve/tls_handler.c with C+ conventions.
 * Provides full certificate lifecycle, TLS handshake, and
 * self-signed certificate generation for development use.
 */

#include "tls_handler.hpp"
#include "serve_utils.hpp"
#include "../../lib/log.h"

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>

#ifdef MBEDTLS_X509_CRT_WRITE_C
#include <mbedtls/x509_csr.h>
#include <mbedtls/x509write_crt.h>
#include <mbedtls/bignum.h>
#include <mbedtls/rsa.h>
#endif

#include <string.h>
#include <time.h>

// ============================================================================
// Global init/cleanup
// ============================================================================

int tls_init(void) {
    log_info("tls subsystem initialized (mbedTLS)");
    return 0;
}

void tls_cleanup(void) {
    log_info("tls subsystem cleaned up");
}

// ============================================================================
// Configuration defaults
// ============================================================================

TlsConfig tls_config_default(void) {
    TlsConfig config;
    memset(&config, 0, sizeof(config));
    config.verify_peer = 0;
    config.min_version = 0; // TLS 1.2
    return config;
}

// ============================================================================
// TLS Context
// ============================================================================

TlsContext* tls_context_create(const TlsConfig *config) {
    TlsContext *ctx = (TlsContext*)serve_calloc(1, sizeof(TlsContext));
    if (!ctx) return NULL;

    // allocate mbedTLS structures
    ctx->ssl_config = (mbedtls_ssl_config*)serve_calloc(1, sizeof(mbedtls_ssl_config));
    ctx->own_cert = (mbedtls_x509_crt*)serve_calloc(1, sizeof(mbedtls_x509_crt));
    ctx->own_key = (mbedtls_pk_context*)serve_calloc(1, sizeof(mbedtls_pk_context));
    ctx->entropy = (mbedtls_entropy_context*)serve_calloc(1, sizeof(mbedtls_entropy_context));
    ctx->ctr_drbg = (mbedtls_ctr_drbg_context*)serve_calloc(1, sizeof(mbedtls_ctr_drbg_context));

    // initialize all mbedTLS contexts
    mbedtls_ssl_config_init(ctx->ssl_config);
    mbedtls_x509_crt_init(ctx->own_cert);
    mbedtls_pk_init(ctx->own_key);
    mbedtls_entropy_init(ctx->entropy);
    mbedtls_ctr_drbg_init(ctx->ctr_drbg);

    // seed the random number generator
    const char *pers = "lambda_tls_server";
    int ret = mbedtls_ctr_drbg_seed(ctx->ctr_drbg, mbedtls_entropy_func,
                                     ctx->entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls ctr_drbg_seed failed: %s", errbuf);
        tls_context_destroy(ctx);
        return NULL;
    }

    // configure SSL defaults (server, TLS, standard I/O)
    ret = mbedtls_ssl_config_defaults(ctx->ssl_config,
                                       MBEDTLS_SSL_IS_SERVER,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls ssl_config_defaults failed: %s", errbuf);
        tls_context_destroy(ctx);
        return NULL;
    }

    // set RNG
    mbedtls_ssl_conf_rng(ctx->ssl_config, mbedtls_ctr_drbg_random, ctx->ctr_drbg);

    // set minimum TLS version
    mbedtls_ssl_conf_min_tls_version(ctx->ssl_config, MBEDTLS_SSL_VERSION_TLS1_2);

    // load certificates if provided
    if (config && config->cert_file && config->key_file) {
        if (tls_load_certificates(ctx, config->cert_file, config->key_file) != 0) {
            tls_context_destroy(ctx);
            return NULL;
        }
    }

    // load CA certificates if provided
    if (config && config->ca_file) {
        tls_set_ca_certificates(ctx, config->ca_file);
    }

    // peer verification
    if (config && config->verify_peer) {
        mbedtls_ssl_conf_authmode(ctx->ssl_config, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(ctx->ssl_config, MBEDTLS_SSL_VERIFY_NONE);
    }

    ctx->initialized = 1;
    log_info("tls context created successfully");
    return ctx;
}

void tls_context_destroy(TlsContext *ctx) {
    if (!ctx) return;

    if (ctx->ssl_config) {
        mbedtls_ssl_config_free(ctx->ssl_config);
        serve_free(ctx->ssl_config);
    }
    if (ctx->own_cert) {
        mbedtls_x509_crt_free(ctx->own_cert);
        serve_free(ctx->own_cert);
    }
    if (ctx->own_key) {
        mbedtls_pk_free(ctx->own_key);
        serve_free(ctx->own_key);
    }
    if (ctx->ca_chain) {
        mbedtls_x509_crt_free(ctx->ca_chain);
        serve_free(ctx->ca_chain);
    }
    if (ctx->ctr_drbg) {
        mbedtls_ctr_drbg_free(ctx->ctr_drbg);
        serve_free(ctx->ctr_drbg);
    }
    if (ctx->entropy) {
        mbedtls_entropy_free(ctx->entropy);
        serve_free(ctx->entropy);
    }

    serve_free(ctx);
}

// ============================================================================
// Certificate loading
// ============================================================================

int tls_load_certificates(TlsContext *ctx, const char *cert_file, const char *key_file) {
    if (!ctx || !cert_file || !key_file) return -1;

    int ret = mbedtls_x509_crt_parse_file(ctx->own_cert, cert_file);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls failed to load certificate '%s': %s", cert_file, errbuf);
        return -1;
    }

    ret = mbedtls_pk_parse_keyfile(ctx->own_key, key_file, NULL,
                                    mbedtls_ctr_drbg_random, ctx->ctr_drbg);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls failed to load private key '%s': %s", key_file, errbuf);
        return -1;
    }

    ret = mbedtls_ssl_conf_own_cert(ctx->ssl_config, ctx->own_cert, ctx->own_key);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls failed to set own certificate: %s", errbuf);
        return -1;
    }

    log_info("tls loaded certificate: %s, key: %s", cert_file, key_file);
    return 0;
}

int tls_set_ca_certificates(TlsContext *ctx, const char *ca_file) {
    if (!ctx || !ca_file) return -1;

    if (!ctx->ca_chain) {
        ctx->ca_chain = (mbedtls_x509_crt*)serve_calloc(1, sizeof(mbedtls_x509_crt));
        mbedtls_x509_crt_init(ctx->ca_chain);
    }

    int ret = mbedtls_x509_crt_parse_file(ctx->ca_chain, ca_file);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls failed to load CA certificates '%s': %s", ca_file, errbuf);
        return -1;
    }

    mbedtls_ssl_conf_ca_chain(ctx->ssl_config, ctx->ca_chain, NULL);
    log_info("tls loaded CA certificates: %s", ca_file);
    return 0;
}

// ============================================================================
// TLS Connection (per-client)
// ============================================================================

TlsConnection* tls_connection_create(TlsContext *ctx, uv_tcp_t *client) {
    if (!ctx || !client || !ctx->initialized) return NULL;

    TlsConnection *conn = (TlsConnection*)serve_calloc(1, sizeof(TlsConnection));
    if (!conn) return NULL;

    conn->ssl = (mbedtls_ssl_context*)serve_calloc(1, sizeof(mbedtls_ssl_context));
    mbedtls_ssl_init(conn->ssl);

    int ret = mbedtls_ssl_setup(conn->ssl, ctx->ssl_config);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls ssl_setup failed: %s", errbuf);
        tls_connection_destroy(conn);
        return NULL;
    }

    conn->ctx = ctx;
    conn->client = client;
    conn->handshake_done = 0;

    return conn;
}

void tls_connection_destroy(TlsConnection *conn) {
    if (!conn) return;
    if (conn->ssl) {
        mbedtls_ssl_free(conn->ssl);
        serve_free(conn->ssl);
    }
    serve_free(conn);
}

// ============================================================================
// TLS handshake
// ============================================================================

int tls_handshake(TlsConnection *conn) {
    if (!conn || !conn->ssl) return -1;
    if (conn->handshake_done) return 0;

    int ret = mbedtls_ssl_handshake(conn->ssl);
    if (ret == 0) {
        conn->handshake_done = 1;
        return 0;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 1; // in progress
    }

    char errbuf[128];
    mbedtls_strerror(ret, errbuf, sizeof(errbuf));
    log_error("tls handshake failed: %s", errbuf);
    return -1;
}

// ============================================================================
// TLS read/write
// ============================================================================

int tls_read(TlsConnection *conn, unsigned char *buf, size_t len) {
    if (!conn || !conn->ssl || !conn->handshake_done) return -1;
    return mbedtls_ssl_read(conn->ssl, buf, len);
}

int tls_write(TlsConnection *conn, const unsigned char *buf, size_t len) {
    if (!conn || !conn->ssl || !conn->handshake_done) return -1;
    return mbedtls_ssl_write(conn->ssl, buf, len);
}

// ============================================================================
// Certificate info
// ============================================================================

char* tls_get_peer_subject(TlsConnection *conn) {
    if (!conn || !conn->ssl) return NULL;
    const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(conn->ssl);
    if (!peer) return NULL;

    char buf[512];
    int ret = mbedtls_x509_dn_gets(buf, sizeof(buf), &peer->subject);
    if (ret < 0) return NULL;

    return serve_strdup(buf);
}

const char* tls_get_cipher_name(TlsConnection *conn) {
    if (!conn || !conn->ssl) return NULL;
    return mbedtls_ssl_get_ciphersuite(conn->ssl);
}

const char* tls_get_protocol_version(TlsConnection *conn) {
    if (!conn || !conn->ssl) return NULL;
    return mbedtls_ssl_get_version(conn->ssl);
}

// ============================================================================
// Self-signed certificate generation
// ============================================================================

#ifdef MBEDTLS_X509_CRT_WRITE_C
int tls_generate_self_signed(const char *cert_path, const char *key_path,
                             const char *common_name, int valid_days) {
    if (!cert_path || !key_path) return -1;
    if (!common_name) common_name = "localhost";
    if (valid_days <= 0) valid_days = 365;

    int ret;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_mpi serial;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_mpi_init(&serial);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char *pers = "lambda_self_signed";
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char*)pers, strlen(pers));
    if (ret != 0) goto cleanup;

    // generate RSA 2048 key pair
    ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) goto cleanup;

    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random,
                               &ctr_drbg, 2048, 65537);
    if (ret != 0) goto cleanup;

    // build subject string: "CN=<common_name>"
    char subject[256];
    snprintf(subject, sizeof(subject), "CN=%s", common_name);

    // set certificate fields
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);

    ret = mbedtls_x509write_crt_set_subject_name(&crt, subject);
    if (ret != 0) goto cleanup;

    ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject);
    if (ret != 0) goto cleanup;

    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

    // serial number
    ret = mbedtls_mpi_lset(&serial, 1);
    if (ret != 0) goto cleanup;
    ret = mbedtls_x509write_crt_set_serial(&crt, &serial);
    if (ret != 0) goto cleanup;

    // validity period
    char not_before[16], not_after[16];
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(not_before, sizeof(not_before), "%Y%m%d%H%M%S", tm_info);

    time_t expires = now + (time_t)valid_days * 86400;
    tm_info = gmtime(&expires);
    strftime(not_after, sizeof(not_after), "%Y%m%d%H%M%S", tm_info);

    ret = mbedtls_x509write_crt_set_validity(&crt, not_before, not_after);
    if (ret != 0) goto cleanup;

    // write certificate to file
    {
        unsigned char buf[4096];
        ret = mbedtls_x509write_crt_pem(&crt, buf, sizeof(buf),
                                          mbedtls_ctr_drbg_random, &ctr_drbg);
        if (ret != 0) goto cleanup;

        FILE *f = fopen(cert_path, "wb");
        if (!f) { ret = -1; goto cleanup; }
        fputs((const char*)buf, f);
        fclose(f);
    }

    // write private key to file
    {
        unsigned char buf[4096];
        ret = mbedtls_pk_write_key_pem(&key, buf, sizeof(buf));
        if (ret != 0) goto cleanup;

        FILE *f = fopen(key_path, "wb");
        if (!f) { ret = -1; goto cleanup; }
        fputs((const char*)buf, f);
        fclose(f);
    }

    log_info("tls generated self-signed certificate: CN=%s, valid %d days", common_name, valid_days);
    ret = 0;

cleanup:
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_mpi_free(&serial);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls self-signed cert generation failed: %s", errbuf);
    }
    return ret;
}
#else
int tls_generate_self_signed(const char *cert_path, const char *key_path,
                             const char *common_name, int valid_days) {
    (void)cert_path; (void)key_path; (void)common_name; (void)valid_days;
    log_error("tls self-signed cert generation not available (mbedTLS compiled without X509_CRT_WRITE)");
    return -1;
}
#endif
