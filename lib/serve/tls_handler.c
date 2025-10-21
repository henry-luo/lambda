/**
 * @file tls_handler.c
 * @brief TLS/SSL handling implementation using mbedTLS
 */

#include "tls_handler.h"
#include "mbedtls_compat.h"
#include "utils.h"
#include <string.h>
#include <stdio.h>

// ssl library initialization flag
static int ssl_initialized = 0;

/**
 * ssl initialization and cleanup implementation
 */

int tls_init(void) {
    if (ssl_initialized) {
        return 0; // already initialized
    }

    // mbedtls doesn't require global initialization like OpenSSL
    // individual contexts will handle their own initialization

    ssl_initialized = 1;
    SERVE_LOG_INFO("mbedtls library initialized");
    return 0;
}

void tls_cleanup(void) {
    if (!ssl_initialized) {
        return;
    }

    // mbedtls doesn't require global cleanup

    ssl_initialized = 0;
    SERVE_LOG_INFO("mbedtls library cleaned up");
}

/**
 * ssl context management implementation
 */

SSL_CTX* tls_create_context(const tls_config_t *config) {
    if (!config) {
        serve_set_error("null configuration");
        return NULL;
    }

    if (!ssl_initialized) {
        if (tls_init() != 0) {
            serve_set_error("failed to initialize ssl library");
            return NULL;
        }
    }

    // allocate ssl context
    SSL_CTX *ctx = (SSL_CTX*)calloc(1, sizeof(SSL_CTX));
    if (!ctx) {
        serve_set_error("failed to allocate ssl context");
        return NULL;
    }

    // initialize mbedtls structures
    mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_x509_crt_init(&ctx->cert);
    mbedtls_pk_init(&ctx->pkey);
    mbedtls_x509_crt_init(&ctx->ca_chain);
    mbedtls_entropy_init(&ctx->entropy);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);

    // seed the random number generator
    const char *pers = "lambda_tls_server";
    int ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func,
                                    &ctx->entropy,
                                    (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        serve_set_error("failed to seed random: %s", error_buf);
        tls_destroy_context(ctx);
        return NULL;
    }

    // configure ssl context as server
    ret = mbedtls_ssl_config_defaults(&ctx->conf,
                                      MBEDTLS_SSL_IS_SERVER,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        serve_set_error("failed to set config defaults: %s", error_buf);
        tls_destroy_context(ctx);
        return NULL;
    }

    // set random number generator
    mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    // set minimum tls version to 1.2
    mbedtls_ssl_conf_min_version(&ctx->conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);

    // load certificates if provided
    if (config->cert_file && config->key_file) {
        if (tls_load_certificates(ctx, config->cert_file, config->key_file) != 0) {
            tls_destroy_context(ctx);
            return NULL;
        }
    }

    // set ca certificates if provided
    if (config->ca_file || config->ca_path) {
        if (tls_set_ca_certificates(ctx, config->ca_file, config->ca_path) != 0) {
            tls_destroy_context(ctx);
            return NULL;
        }
    }

    // set cipher list if provided
    if (config->cipher_list) {
        if (tls_set_cipher_list(ctx, config->cipher_list) != 0) {
            tls_destroy_context(ctx);
            return NULL;
        }
    }

    // configure verification
    tls_set_verify(ctx, config->verify_peer, config->verify_depth);

    ctx->initialized = 1;
    SERVE_LOG_INFO("ssl context created successfully");
    return ctx;
}

void tls_destroy_context(SSL_CTX *ctx) {
    if (ctx) {
        if (ctx->initialized) {
            mbedtls_ssl_config_free(&ctx->conf);
            mbedtls_x509_crt_free(&ctx->cert);
            mbedtls_pk_free(&ctx->pkey);
            mbedtls_x509_crt_free(&ctx->ca_chain);
            mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
            mbedtls_entropy_free(&ctx->entropy);
        }
        free(ctx);
        SERVE_LOG_DEBUG("ssl context destroyed");
    }
}

int tls_load_certificates(SSL_CTX *ctx, const char *cert_file,
                         const char *key_file) {
    if (!ctx || !cert_file || !key_file) {
        serve_set_error("invalid parameters");
        return -1;
    }

    // check if files exist
    if (!serve_file_exists(cert_file)) {
        serve_set_error("certificate file not found: %s", cert_file);
        return -1;
    }

    if (!serve_file_exists(key_file)) {
        serve_set_error("private key file not found: %s", key_file);
        return -1;
    }

    // load certificate
    int ret = mbedtls_x509_crt_parse_file(&ctx->cert, cert_file);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        serve_set_error("failed to load certificate: %s (%s)", cert_file, error_buf);
        return -1;
    }

    // load private key - mbedTLS 3.x requires RNG parameters
    ret = mbedtls_pk_parse_keyfile(&ctx->pkey, key_file, NULL, NULL, NULL);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        serve_set_error("failed to load private key: %s (%s)", key_file, error_buf);
        return -1;
    }

    // verify that certificate and private key match
    // mbedtls_pk_can_do and comparison logic would go here
    // for simplicity, we'll trust they match if both loaded successfully

    // set certificate and key in config
    ret = mbedtls_ssl_conf_own_cert(&ctx->conf, &ctx->cert, &ctx->pkey);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        serve_set_error("failed to set certificate and key: %s", error_buf);
        return -1;
    }

    SERVE_LOG_INFO("certificates loaded successfully");
    return 0;
}

int tls_set_ca_certificates(SSL_CTX *ctx, const char *ca_file,
                           const char *ca_path) {
    if (!ctx) {
        serve_set_error("null ssl context");
        return -1;
    }

    if (!ca_file && !ca_path) {
        serve_set_error("no ca file or path specified");
        return -1;
    }

    // mbedtls doesn't support directory paths directly, only files
    if (ca_file) {
        int ret = mbedtls_x509_crt_parse_file(&ctx->ca_chain, ca_file);
        if (ret != 0) {
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            serve_set_error("failed to load ca certificates: %s", error_buf);
            return -1;
        }

        mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->ca_chain, NULL);
    }

    if (ca_path) {
        SERVE_LOG_WARN("ca_path not supported by mbedtls, only ca_file is used");
    }

    SERVE_LOG_INFO("ca certificates loaded successfully");
    return 0;
}

int tls_set_cipher_list(SSL_CTX *ctx, const char *cipher_list) {
    if (!ctx || !cipher_list) {
        serve_set_error("invalid parameters");
        return -1;
    }

    // mbedtls cipher suite configuration is more complex than openssl
    // for now, we'll log and use defaults
    // a full implementation would parse the cipher_list and map to mbedtls cipher suites
    SERVE_LOG_DEBUG("cipher list requested: %s (using mbedtls defaults)", cipher_list);

    // TODO: implement cipher suite mapping from OpenSSL format to mbedTLS

    return 0;
}

void tls_set_verify(SSL_CTX *ctx, int verify_peer, int verify_depth) {
    if (!ctx) {
        return;
    }

    if (verify_peer) {
        mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    // mbedtls doesn't have a direct equivalent to verify_depth
    // it will verify the full chain by default

    SERVE_LOG_DEBUG("ssl verification configured: peer=%d, depth=%d",
                   verify_peer, verify_depth);
}

/**
 * ssl connection management implementation
 *
 * NOTE: libevent's bufferevent_openssl_* functions are OpenSSL-specific.
 * To properly integrate mbedTLS with libevent, you'll need to either:
 * 1. Use libevent's generic bufferevent and handle SSL I/O manually
 * 2. Create a custom bufferevent filter for mbedTLS
 * 3. Use a library that provides mbedTLS support for libevent
 *
 * The functions below provide a placeholder API that maintains compatibility
 * with the existing interface, but will need proper implementation.
 */

struct bufferevent* tls_create_bufferevent(struct event_base *base,
                                          SSL_CTX *ctx, evutil_socket_t socket,
                                          enum bufferevent_ssl_state state) {
    if (!base || !ctx) {
        serve_set_error("invalid parameters");
        return NULL;
    }

    // TODO: Implement mbedTLS integration with libevent
    // This requires creating a custom bufferevent or using a compatibility layer
    serve_set_error("mbedtls bufferevent integration not yet implemented");
    return NULL;

    /* Example approach:
    // Create a new SSL connection
    SSL *ssl = calloc(1, sizeof(SSL));
    if (!ssl) return NULL;

    mbedtls_ssl_init(&ssl->ssl);
    ssl->ctx = ctx;
    ssl->socket_fd = socket;

    // Setup SSL with the context configuration
    mbedtls_ssl_setup(&ssl->ssl, &ctx->conf);

    // Set the socket
    mbedtls_ssl_set_bio(&ssl->ssl, &socket, mbedtls_net_send, mbedtls_net_recv, NULL);

    // Create a bufferevent socket and wrap it with SSL handling
    // This would require a custom implementation
    */
}

SSL* tls_get_ssl(struct bufferevent *bev) {
    if (!bev) {
        return NULL;
    }

    // TODO: Extract SSL from bufferevent
    // This depends on the bufferevent implementation
    return NULL;
}

X509* tls_get_peer_certificate(SSL *ssl) {
    if (!ssl) {
        return NULL;
    }

    // mbedtls provides peer certificate via mbedtls_ssl_get_peer_cert()
    // which returns a const pointer, not owned by caller
    const mbedtls_x509_crt *peer_cert = mbedtls_ssl_get_peer_cert(&ssl->ssl);

    // For compatibility, we'd need to clone this
    // For now, return the const pointer cast (not ideal)
    return (X509*)peer_cert;
}

const char* tls_get_cipher_name(SSL *ssl) {
    if (!ssl) {
        return NULL;
    }

    return mbedtls_ssl_get_ciphersuite(&ssl->ssl);
}

const char* tls_get_protocol_version(SSL *ssl) {
    if (!ssl) {
        return NULL;
    }

    return mbedtls_ssl_get_version(&ssl->ssl);
}

/**
 * ssl error handling implementation
 */

const char* tls_get_error_string(SSL *ssl, int ret) {
    static char error_buf[100];

    if (ret >= 0) {
        return "no error";
    }

    // mbedtls error codes are negative
    mbedtls_strerror(ret, error_buf, sizeof(error_buf));
    return error_buf;
}

void tls_log_errors(const char *prefix) {
    // mbedtls doesn't have an error queue like OpenSSL
    // errors are returned directly from functions
    SERVE_LOG_DEBUG("%s: check return codes from mbedtls functions",
                   prefix ? prefix : "ssl error");
}

/**
 * ssl utility functions implementation
 */

tls_config_t tls_config_default(void) {
    tls_config_t config = {0};
    config.verify_peer = 0;
    config.verify_depth = 9;
    config.session_cache_size = 1024;
    config.session_timeout = 300;
    return config;
}

int tls_config_validate(const tls_config_t *config) {
    if (!config) {
        serve_set_error("null configuration");
        return -1;
    }

    // check certificate files if provided
    if (config->cert_file) {
        if (!serve_file_exists(config->cert_file)) {
            serve_set_error("certificate file not found: %s", config->cert_file);
            return -1;
        }

        if (!tls_is_valid_certificate(config->cert_file)) {
            serve_set_error("invalid certificate file: %s", config->cert_file);
            return -1;
        }
    }

    if (config->key_file) {
        if (!serve_file_exists(config->key_file)) {
            serve_set_error("private key file not found: %s", config->key_file);
            return -1;
        }

        if (!tls_is_valid_private_key(config->key_file)) {
            serve_set_error("invalid private key file: %s", config->key_file);
            return -1;
        }
    }

    // check that both cert and key are provided together
    if ((config->cert_file && !config->key_file) ||
        (!config->cert_file && config->key_file)) {
        serve_set_error("both certificate and private key files must be provided");
        return -1;
    }

    // check that cert and key match if both provided
    if (config->cert_file && config->key_file) {
        if (!tls_certificate_key_match(config->cert_file, config->key_file)) {
            serve_set_error("certificate and private key do not match");
            return -1;
        }
    }

    return 0;
}

void tls_config_cleanup(tls_config_t *config) {
    if (!config) {
        return;
    }

    serve_free(config->cert_file);
    serve_free(config->key_file);
    serve_free(config->ca_file);
    serve_free(config->ca_path);
    serve_free(config->cipher_list);

    memset(config, 0, sizeof(tls_config_t));
}

int tls_is_valid_certificate(const char *cert_file) {
    if (!cert_file || !serve_file_exists(cert_file)) {
        return 0;
    }

    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);

    int ret = mbedtls_x509_crt_parse_file(&cert, cert_file);
    mbedtls_x509_crt_free(&cert);

    return (ret == 0) ? 1 : 0;
}

int tls_is_valid_private_key(const char *key_file) {
    if (!key_file || !serve_file_exists(key_file)) {
        return 0;
    }

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);

    // mbedTLS 3.x requires RNG parameters even for NULL password
    int ret = mbedtls_pk_parse_keyfile(&key, key_file, NULL, NULL, NULL);
    mbedtls_pk_free(&key);

    return (ret == 0) ? 1 : 0;
}

int tls_certificate_key_match(const char *cert_file, const char *key_file) {
    if (!cert_file || !key_file) {
        return 0;
    }

    // load certificate
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);

    int ret = mbedtls_x509_crt_parse_file(&cert, cert_file);
    if (ret != 0) {
        mbedtls_x509_crt_free(&cert);
        return 0;
    }

    // load private key
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);

    ret = mbedtls_pk_parse_keyfile(&key, key_file, NULL, NULL, NULL);
    if (ret != 0) {
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&key);
        return 0;
    }

    // check if they match by comparing the public key from cert with the key
    // mbedTLS 3.x requires RNG parameters for pk_check_pair
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char *pers = "cert_key_match";
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                         (const unsigned char*)pers, strlen(pers));

    int match = mbedtls_pk_check_pair(&cert.pk, &key,
                                      mbedtls_ctr_drbg_random, &ctr_drbg) == 0;

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509_crt_free(&cert);
    mbedtls_pk_free(&key);

    return match;
}

int tls_generate_self_signed_cert(const char *cert_file, const char *key_file,
                                 int days, const char *common_name) {
    if (!cert_file || !key_file || !common_name) {
        serve_set_error("invalid parameters");
        return -1;
    }

    // ensure ssl is initialized
    if (!ssl_initialized) {
        if (tls_init() != 0) {
            return -1;
        }
    }

    int ret;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_mpi serial;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "lambda_cert_gen";

    // initialize structures
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_mpi_init(&serial);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // seed random number generator
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        serve_set_error("failed to seed random number generator");
        goto cleanup;
    }

    // generate rsa key pair (2048 bits)
    ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        serve_set_error("failed to setup pk context");
        goto cleanup;
    }

    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random,
                              &ctr_drbg, 2048, 65537);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        serve_set_error("failed to generate rsa key: %s", error_buf);
        goto cleanup;
    }

    // set certificate parameters
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);

    // set subject and issuer name
    char subject_name[256];
    snprintf(subject_name, sizeof(subject_name), "CN=%s,O=Jubily,C=US", common_name);

    ret = mbedtls_x509write_crt_set_subject_name(&crt, subject_name);
    if (ret != 0) {
        serve_set_error("failed to set subject name");
        goto cleanup;
    }

    ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject_name);
    if (ret != 0) {
        serve_set_error("failed to set issuer name");
        goto cleanup;
    }

    // set serial number
    ret = mbedtls_mpi_lset(&serial, 1);
    if (ret != 0) {
        serve_set_error("failed to set serial number");
        goto cleanup;
    }

    ret = mbedtls_x509write_crt_set_serial(&crt, &serial);
    if (ret != 0) {
        serve_set_error("failed to set certificate serial");
        goto cleanup;
    }

    // set validity period
    char not_before[16], not_after[16];
    time_t now = time(NULL);
    struct tm *tm_now = gmtime(&now);
    strftime(not_before, sizeof(not_before), "%Y%m%d%H%M%S", tm_now);

    time_t expiry = now + (days * 24 * 60 * 60);
    struct tm *tm_expiry = gmtime(&expiry);
    strftime(not_after, sizeof(not_after), "%Y%m%d%H%M%S", tm_expiry);

    ret = mbedtls_x509write_crt_set_validity(&crt, not_before, not_after);
    if (ret != 0) {
        serve_set_error("failed to set validity period");
        goto cleanup;
    }

    // set md algorithm
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

    // set basic constraints (CA:FALSE)
    ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    if (ret != 0) {
        serve_set_error("failed to set basic constraints");
        goto cleanup;
    }

    // set key usage
    ret = mbedtls_x509write_crt_set_key_usage(&crt,
                                              MBEDTLS_X509_KU_DIGITAL_SIGNATURE |
                                              MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
    if (ret != 0) {
        serve_set_error("failed to set key usage");
        goto cleanup;
    }

    // write certificate to file (write to buffer then to file)
    unsigned char cert_buf[4096];
    ret = mbedtls_x509write_crt_pem(&crt, cert_buf, sizeof(cert_buf),
                                   mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        serve_set_error("failed to write certificate: %s", error_buf);
        goto cleanup;
    }

    // write certificate buffer to file
    FILE *cert_fp = fopen(cert_file, "w");
    if (!cert_fp) {
        serve_set_error("failed to open certificate file for writing");
        ret = -1;
        goto cleanup;
    }
    fputs((char*)cert_buf, cert_fp);
    fclose(cert_fp);

    // write private key to file (write to buffer then to file)
    unsigned char key_buf[4096];
    ret = mbedtls_pk_write_key_pem(&key, key_buf, sizeof(key_buf));
    if (ret != 0) {
        char error_buf[100];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        serve_set_error("failed to write private key: %s", error_buf);
        goto cleanup;
    }

    // write key buffer to file
    FILE *key_fp = fopen(key_file, "w");
    if (!key_fp) {
        serve_set_error("failed to open key file for writing");
        ret = -1;
        goto cleanup;
    }
    fputs((char*)key_buf, key_fp);
    fclose(key_fp);

    SERVE_LOG_INFO("self-signed certificate generated: %s", cert_file);
    ret = 0;

cleanup:
    mbedtls_pk_free(&key);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_mpi_free(&serial);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return (ret == 0) ? 0 : -1;
}
