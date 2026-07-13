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
// In mbedTLS 3.x, x509 write functions are declared in x509_crt.h directly
#if __has_include(<mbedtls/x509write_crt.h>)
#include <mbedtls/x509write_crt.h>
#endif
#include <mbedtls/bignum.h>
#include <mbedtls/rsa.h>
#endif

#include <string.h>
#include <time.h>

static int tls_uv_send(void *ctx, const unsigned char *buf, size_t len);
static int tls_uv_recv(void *ctx, unsigned char *buf, size_t len);

static int tls_cipher_name_to_id(const char* name, int len) {
    if (!name || len <= 0) return 0;
    char buf[128];
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    int start = 0;
    while (start < len && (name[start] == ' ' || name[start] == '\t')) start++;
    int end = len;
    while (end > start && (name[end - 1] == ' ' || name[end - 1] == '\t')) end--;
    int out_len = end - start;
    if (out_len <= 0) return 0;
    memcpy(buf, name + start, (size_t)out_len);
    buf[out_len] = '\0';

    int id = mbedtls_ssl_get_ciphersuite_id(buf);
    if (id != 0) return id;

    char iana[128];
    int pos = 0;
    for (int i = 0; i < out_len && pos < (int)sizeof(iana) - 1; i++) {
        char c = buf[i];
        iana[pos++] = c == '_' ? '-' : c;
    }
    iana[pos] = '\0';
    id = mbedtls_ssl_get_ciphersuite_id(iana);
    if (id != 0) return id;

    if (strncmp(buf, "ECDHE-ECDSA-AES256-GCM-SHA384", 30) == 0) {
        return MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384;
    }
    if (strncmp(buf, "ECDHE-RSA-AES256-GCM-SHA384", 28) == 0) {
        return MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384;
    }
    return 0;
}

static void tls_apply_cipher_list(TlsContext* ctx, const char* cipher_list) {
    if (!ctx || !ctx->ssl_config || !cipher_list || cipher_list[0] == '\0') return;
    int count = 0;
    const char* token = cipher_list;
    for (const char* p = cipher_list; ; p++) {
        if (*p == ':' || *p == ',' || *p == ' ' || *p == '\0') {
            int len = (int)(p - token);
            int id = tls_cipher_name_to_id(token, len);
            if (id != 0 && count < (int)(sizeof(ctx->ciphersuites) / sizeof(ctx->ciphersuites[0])) - 1) {
                ctx->ciphersuites[count++] = id;
            }
            if (*p == '\0') break;
            token = p + 1;
        }
    }
    if (count > 0) {
        // Node clients expect `ciphers` to constrain the handshake; leaving the
        // dormant config field unused makes getCipher() report unrelated suites.
        ctx->ciphersuites[count] = 0;
        mbedtls_ssl_conf_ciphersuites(ctx->ssl_config, ctx->ciphersuites);
    }
}

typedef struct TlsUvWriteReq {
    uv_write_t req;
    char* data;
} TlsUvWriteReq;

static void tls_uv_write_done(uv_write_t* req, int status) {
    (void)status;
    TlsUvWriteReq* write_req = (TlsUvWriteReq*)req;
    if (!write_req) return;
    if (write_req->data) serve_free(write_req->data);
    serve_free(write_req);
}

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

    // configure SSL defaults (server/client, TLS, standard I/O)
    ret = mbedtls_ssl_config_defaults(ctx->ssl_config,
                                       (config && config->is_client) ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
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

    if (config && config->cipher_list) {
        tls_apply_cipher_list(ctx, config->cipher_list);
    }

    // set minimum TLS version
    mbedtls_ssl_conf_min_tls_version(ctx->ssl_config, MBEDTLS_SSL_VERSION_TLS1_2);
    if (config && config->cipher_list) {
        // Node/OpenSSL cipher names in this layer currently map to TLS 1.2
        // suites; without the matching max version mbedTLS can fail before
        // considering the constrained suite list.
        mbedtls_ssl_conf_max_tls_version(ctx->ssl_config, MBEDTLS_SSL_VERSION_TLS1_2);
    }

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
        // Node reports authorization on TLSSocket; optional verification lets
        // mbedTLS compute trust flags without aborting the handshake early.
        mbedtls_ssl_conf_authmode(ctx->ssl_config, MBEDTLS_SSL_VERIFY_OPTIONAL);
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
    for (int i = 0; i < ctx->extra_cert_count; i++) {
        if (ctx->extra_certs[i]) {
            mbedtls_x509_crt_free(ctx->extra_certs[i]);
            serve_free(ctx->extra_certs[i]);
        }
        if (ctx->extra_keys[i]) {
            mbedtls_pk_free(ctx->extra_keys[i]);
            serve_free(ctx->extra_keys[i]);
        }
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

static int tls_input_is_inline_pem(const char *input) {
    return input && strstr(input, "-----BEGIN") != NULL;
}

static int tls_parse_cert_input(mbedtls_x509_crt *cert, const char *input) {
    if (tls_input_is_inline_pem(input)) {
        return mbedtls_x509_crt_parse(cert, (const unsigned char*)input, strlen(input) + 1);
    }
    return mbedtls_x509_crt_parse_file(cert, input);
}

static int tls_parse_key_input(mbedtls_pk_context *key, const char *input,
                               mbedtls_ctr_drbg_context *ctr_drbg) {
    if (tls_input_is_inline_pem(input)) {
        return mbedtls_pk_parse_key(key, (const unsigned char*)input, strlen(input) + 1,
                                    NULL, 0, mbedtls_ctr_drbg_random, ctr_drbg);
    }
    return mbedtls_pk_parse_keyfile(key, input, NULL, mbedtls_ctr_drbg_random, ctr_drbg);
}

static const char* tls_input_log_label(const char *input) {
    return tls_input_is_inline_pem(input) ? "<inline PEM>" : input;
}

static int tls_load_cert_pair_into(TlsContext *ctx, mbedtls_x509_crt *cert,
                                   mbedtls_pk_context *key,
                                   const char *cert_file, const char *key_file) {
    if (!ctx || !cert_file || !key_file) return -1;

    int ret = tls_parse_cert_input(cert, cert_file);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls failed to load certificate '%s': %s", tls_input_log_label(cert_file), errbuf);
        return -1;
    }

    ret = tls_parse_key_input(key, key_file, ctx->ctr_drbg);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls failed to load private key '%s': %s", tls_input_log_label(key_file), errbuf);
        return -1;
    }

    ret = mbedtls_ssl_conf_own_cert(ctx->ssl_config, cert, key);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls failed to set own certificate: %s", errbuf);
        return -1;
    }

    log_info("tls loaded certificate: %s, key: %s",
             tls_input_log_label(cert_file), tls_input_log_label(key_file));
    return 0;
}

int tls_load_certificates(TlsContext *ctx, const char *cert_file, const char *key_file) {
    if (!ctx) return -1;
    return tls_load_cert_pair_into(ctx, ctx->own_cert, ctx->own_key, cert_file, key_file);
}

int tls_add_certificates(TlsContext *ctx, const char *cert_file, const char *key_file) {
    if (!ctx || !cert_file || !key_file) return -1;
    if (ctx->extra_cert_count >= (int)(sizeof(ctx->extra_certs) / sizeof(ctx->extra_certs[0]))) {
        log_error("tls cannot load more certificate identities");
        return -1;
    }

    mbedtls_x509_crt* cert = (mbedtls_x509_crt*)serve_calloc(1, sizeof(mbedtls_x509_crt));
    mbedtls_pk_context* key = (mbedtls_pk_context*)serve_calloc(1, sizeof(mbedtls_pk_context));
    if (!cert || !key) {
        if (cert) serve_free(cert);
        if (key) serve_free(key);
        return -1;
    }
    mbedtls_x509_crt_init(cert);
    mbedtls_pk_init(key);

    // Multiple TLS identities are appended to mbedTLS's config so SNI/cipher
    // selection can choose RSA or ECDSA instead of silently keeping only one.
    if (tls_load_cert_pair_into(ctx, cert, key, cert_file, key_file) != 0) {
        mbedtls_x509_crt_free(cert);
        mbedtls_pk_free(key);
        serve_free(cert);
        serve_free(key);
        return -1;
    }
    int index = ctx->extra_cert_count++;
    ctx->extra_certs[index] = cert;
    ctx->extra_keys[index] = key;
    return 0;
}

int tls_set_ca_certificates(TlsContext *ctx, const char *ca_file) {
    if (!ctx || !ca_file) return -1;

    if (!ctx->ca_chain) {
        ctx->ca_chain = (mbedtls_x509_crt*)serve_calloc(1, sizeof(mbedtls_x509_crt));
        mbedtls_x509_crt_init(ctx->ca_chain);
    }

    int ret = tls_parse_cert_input(ctx->ca_chain, ca_file);
    if (ret != 0) {
        char errbuf[128];
        mbedtls_strerror(ret, errbuf, sizeof(errbuf));
        log_error("tls failed to load CA certificates '%s': %s", tls_input_log_label(ca_file), errbuf);
        return -1;
    }

    mbedtls_ssl_conf_ca_chain(ctx->ssl_config, ctx->ca_chain, NULL);
    log_info("tls loaded CA certificates: %s", tls_input_log_label(ca_file));
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
    // mbedTLS does not know about libuv handles unless explicit BIO callbacks
    // bridge encrypted records into and out of the event-loop transport.
    mbedtls_ssl_set_bio(conn->ssl, conn, tls_uv_send, tls_uv_recv, NULL);

    return conn;
}

void tls_connection_destroy(TlsConnection *conn) {
    if (!conn) return;
    if (conn->ssl) {
        mbedtls_ssl_free(conn->ssl);
        serve_free(conn->ssl);
    }
    if (conn->input) serve_free(conn->input);
    serve_free(conn);
}

int tls_connection_feed(TlsConnection *conn, const unsigned char *buf, size_t len) {
    if (!conn || !buf || len == 0) return 0;

    size_t pending = conn->input_len > conn->input_pos ? conn->input_len - conn->input_pos : 0;
    if (pending == 0) {
        conn->input_pos = 0;
        conn->input_len = 0;
    }

    unsigned char *next = (unsigned char*)serve_malloc(pending + len);
    if (!next) return -1;
    if (pending > 0) memcpy(next, conn->input + conn->input_pos, pending);
    memcpy(next + pending, buf, len);
    if (conn->input) serve_free(conn->input);
    conn->input = next;
    conn->input_pos = 0;
    conn->input_len = pending + len;
    return 0;
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

static int tls_uv_send(void *ctx, const unsigned char *buf, size_t len) {
    TlsConnection *conn = (TlsConnection*)ctx;
    if (!conn || !conn->client || !buf || len == 0) return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    TlsUvWriteReq* write_req = (TlsUvWriteReq*)serve_calloc(1, sizeof(TlsUvWriteReq));
    if (!write_req) return MBEDTLS_ERR_SSL_ALLOC_FAILED;
    write_req->data = (char*)serve_malloc(len);
    if (!write_req->data) {
        serve_free(write_req);
        return MBEDTLS_ERR_SSL_ALLOC_FAILED;
    }
    memcpy(write_req->data, buf, len);
    uv_buf_t uvbuf = uv_buf_init(write_req->data, (unsigned int)len);
    // mbedTLS owns `buf` only until the BIO callback returns; queueing a copy
    // prevents partial try_write progress from losing encrypted TLS records.
    int ret = uv_write(&write_req->req, (uv_stream_t*)conn->client, &uvbuf, 1, tls_uv_write_done);
    if (ret < 0) {
        if (write_req->data) serve_free(write_req->data);
        serve_free(write_req);
        return ret == UV_EAGAIN ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return (int)len;
}

static int tls_uv_recv(void *ctx, unsigned char *buf, size_t len) {
    TlsConnection *conn = (TlsConnection*)ctx;
    if (!conn || !buf || len == 0) return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    if (!conn->input || conn->input_pos >= conn->input_len) return MBEDTLS_ERR_SSL_WANT_READ;

    size_t available = conn->input_len - conn->input_pos;
    size_t take = available < len ? available : len;
    memcpy(buf, conn->input + conn->input_pos, take);
    conn->input_pos += take;
    if (conn->input_pos >= conn->input_len) {
        serve_free(conn->input);
        conn->input = NULL;
        conn->input_pos = 0;
        conn->input_len = 0;
    }
    return (int)take;
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

unsigned int tls_get_verify_result(TlsConnection *conn) {
    if (!conn || !conn->ssl || !conn->handshake_done) return (unsigned int)-1;
    return (unsigned int)mbedtls_ssl_get_verify_result(conn->ssl);
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
    char subject[256];
    char not_before[16], not_after[16];
    time_t now;
    struct tm *tm_info;
    time_t expires;

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
    now = time(NULL);
    tm_info = gmtime(&now);
    strftime(not_before, sizeof(not_before), "%Y%m%d%H%M%S", tm_info);

    expires = now + (time_t)valid_days * 86400;
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
