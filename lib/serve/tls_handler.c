/**
 * @file tls_handler.c
 * @brief TLS/SSL handling implementation
 */

#include "tls_handler.h"
#include "utils.h"
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>

// ssl library initialization flag
static int ssl_initialized = 0;

/**
 * ssl initialization and cleanup implementation
 */

int tls_init(void) {
    if (ssl_initialized) {
        return 0; // already initialized
    }
    
    // initialize ssl library
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // seed random number generator
    if (RAND_load_file("/dev/urandom", 32) != 32) {
        SERVE_LOG_WARN("failed to seed random number generator");
    }
    
    ssl_initialized = 1;
    SERVE_LOG_INFO("ssl library initialized");
    return 0;
}

void tls_cleanup(void) {
    if (!ssl_initialized) {
        return;
    }
    
    EVP_cleanup();
    ERR_free_strings();
    
    ssl_initialized = 0;
    SERVE_LOG_INFO("ssl library cleaned up");
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
    
    // create ssl context
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        tls_log_errors("failed to create ssl context");
        return NULL;
    }
    
    // set secure defaults
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | 
                            SSL_OP_NO_COMPRESSION | SSL_OP_SINGLE_DH_USE |
                            SSL_OP_SINGLE_ECDH_USE | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);
    
    // set minimum protocol version to TLS 1.2
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    
    // load certificates if provided
    if (config->cert_file && config->key_file) {
        if (tls_load_certificates(ctx, config->cert_file, config->key_file) != 0) {
            SSL_CTX_free(ctx);
            return NULL;
        }
    }
    
    // set ca certificates if provided
    if (config->ca_file || config->ca_path) {
        if (tls_set_ca_certificates(ctx, config->ca_file, config->ca_path) != 0) {
            SSL_CTX_free(ctx);
            return NULL;
        }
    }
    
    // set cipher list if provided
    if (config->cipher_list) {
        if (tls_set_cipher_list(ctx, config->cipher_list) != 0) {
            SSL_CTX_free(ctx);
            return NULL;
        }
    } else {
        // use secure default cipher list
        const char *default_ciphers = "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS";
        tls_set_cipher_list(ctx, default_ciphers);
    }
    
    // configure verification
    tls_set_verify(ctx, config->verify_peer, config->verify_depth);
    
    // set session cache
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
    if (config->session_cache_size > 0) {
        SSL_CTX_sess_set_cache_size(ctx, config->session_cache_size);
    }
    if (config->session_timeout > 0) {
        SSL_CTX_set_timeout(ctx, config->session_timeout);
    }
    
    SERVE_LOG_INFO("ssl context created successfully");
    return ctx;
}

void tls_destroy_context(SSL_CTX *ctx) {
    if (ctx) {
        SSL_CTX_free(ctx);
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
    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) != 1) {
        tls_log_errors("failed to load certificate file");
        serve_set_error("failed to load certificate: %s", cert_file);
        return -1;
    }
    
    // load private key
    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1) {
        tls_log_errors("failed to load private key file");
        serve_set_error("failed to load private key: %s", key_file);
        return -1;
    }
    
    // verify that certificate and private key match
    if (SSL_CTX_check_private_key(ctx) != 1) {
        tls_log_errors("certificate and private key mismatch");
        serve_set_error("certificate and private key do not match");
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
    
    if (SSL_CTX_load_verify_locations(ctx, ca_file, ca_path) != 1) {
        tls_log_errors("failed to load ca certificates");
        serve_set_error("failed to load ca certificates");
        return -1;
    }
    
    SERVE_LOG_INFO("ca certificates loaded successfully");
    return 0;
}

int tls_set_cipher_list(SSL_CTX *ctx, const char *cipher_list) {
    if (!ctx || !cipher_list) {
        serve_set_error("invalid parameters");
        return -1;
    }
    
    if (SSL_CTX_set_cipher_list(ctx, cipher_list) != 1) {
        tls_log_errors("failed to set cipher list");
        serve_set_error("failed to set cipher list: %s", cipher_list);
        return -1;
    }
    
    SERVE_LOG_DEBUG("cipher list set: %s", cipher_list);
    return 0;
}

void tls_set_verify(SSL_CTX *ctx, int verify_peer, int verify_depth) {
    if (!ctx) {
        return;
    }
    
    int verify_mode = SSL_VERIFY_NONE;
    if (verify_peer) {
        verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    }
    
    SSL_CTX_set_verify(ctx, verify_mode, NULL);
    
    if (verify_depth > 0) {
        SSL_CTX_set_verify_depth(ctx, verify_depth);
    }
    
    SERVE_LOG_DEBUG("ssl verification configured: peer=%d, depth=%d", 
                   verify_peer, verify_depth);
}

/**
 * ssl connection management implementation
 */

struct bufferevent* tls_create_bufferevent(struct event_base *base, 
                                          SSL_CTX *ctx, evutil_socket_t socket,
                                          enum bufferevent_ssl_state state) {
    if (!base || !ctx) {
        serve_set_error("invalid parameters");
        return NULL;
    }
    
    struct bufferevent *bev = bufferevent_openssl_socket_new(base, socket, 
                                                            SSL_new(ctx), 
                                                            state, 
                                                            BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        serve_set_error("failed to create ssl bufferevent");
        return NULL;
    }
    
    return bev;
}

SSL* tls_get_ssl(struct bufferevent *bev) {
    if (!bev) {
        return NULL;
    }
    
    return bufferevent_openssl_get_ssl(bev);
}

X509* tls_get_peer_certificate(SSL *ssl) {
    if (!ssl) {
        return NULL;
    }
    
    return SSL_get_peer_certificate(ssl);
}

const char* tls_get_cipher_name(SSL *ssl) {
    if (!ssl) {
        return NULL;
    }
    
    return SSL_get_cipher_name(ssl);
}

const char* tls_get_protocol_version(SSL *ssl) {
    if (!ssl) {
        return NULL;
    }
    
    return SSL_get_version(ssl);
}

/**
 * ssl error handling implementation
 */

const char* tls_get_error_string(SSL *ssl, int ret) {
    if (!ssl) {
        return "unknown ssl error";
    }
    
    int error = SSL_get_error(ssl, ret);
    
    switch (error) {
        case SSL_ERROR_NONE:
            return "no error";
        case SSL_ERROR_SSL:
            return "ssl protocol error";
        case SSL_ERROR_WANT_READ:
            return "ssl wants read";
        case SSL_ERROR_WANT_WRITE:
            return "ssl wants write";
        case SSL_ERROR_WANT_X509_LOOKUP:
            return "ssl wants x509 lookup";
        case SSL_ERROR_SYSCALL:
            return "ssl syscall error";
        case SSL_ERROR_ZERO_RETURN:
            return "ssl connection closed";
        case SSL_ERROR_WANT_CONNECT:
            return "ssl wants connect";
        case SSL_ERROR_WANT_ACCEPT:
            return "ssl wants accept";
        default:
            return "unknown ssl error";
    }
}

void tls_log_errors(const char *prefix) {
    unsigned long error;
    while ((error = ERR_get_error()) != 0) {
        char error_string[256];
        ERR_error_string_n(error, error_string, sizeof(error_string));
        SERVE_LOG_ERROR("%s: %s", prefix ? prefix : "ssl error", error_string);
    }
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
    
    FILE *file = fopen(cert_file, "r");
    if (!file) {
        return 0;
    }
    
    X509 *cert = PEM_read_X509(file, NULL, NULL, NULL);
    fclose(file);
    
    if (cert) {
        X509_free(cert);
        return 1;
    }
    
    return 0;
}

int tls_is_valid_private_key(const char *key_file) {
    if (!key_file || !serve_file_exists(key_file)) {
        return 0;
    }
    
    FILE *file = fopen(key_file, "r");
    if (!file) {
        return 0;
    }
    
    EVP_PKEY *key = PEM_read_PrivateKey(file, NULL, NULL, NULL);
    fclose(file);
    
    if (key) {
        EVP_PKEY_free(key);
        return 1;
    }
    
    return 0;
}

int tls_certificate_key_match(const char *cert_file, const char *key_file) {
    if (!cert_file || !key_file) {
        return 0;
    }
    
    // load certificate
    FILE *cert_fp = fopen(cert_file, "r");
    if (!cert_fp) {
        return 0;
    }
    
    X509 *cert = PEM_read_X509(cert_fp, NULL, NULL, NULL);
    fclose(cert_fp);
    
    if (!cert) {
        return 0;
    }
    
    // load private key
    FILE *key_fp = fopen(key_file, "r");
    if (!key_fp) {
        X509_free(cert);
        return 0;
    }
    
    EVP_PKEY *key = PEM_read_PrivateKey(key_fp, NULL, NULL, NULL);
    fclose(key_fp);
    
    if (!key) {
        X509_free(cert);
        return 0;
    }
    
    // check if they match
    EVP_PKEY *cert_key = X509_get_pubkey(cert);
    int match = 0;
    
    if (cert_key) {
        match = EVP_PKEY_cmp(cert_key, key) == 1;
        EVP_PKEY_free(cert_key);
    }
    
    X509_free(cert);
    EVP_PKEY_free(key);
    
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
    
    // generate rsa key pair
    RSA *rsa = RSA_new();
    BIGNUM *bn = BN_new();
    
    if (!rsa || !bn) {
        serve_set_error("failed to allocate rsa structures");
        goto cleanup;
    }
    
    if (BN_set_word(bn, RSA_F4) != 1) {
        serve_set_error("failed to set rsa exponent");
        goto cleanup;
    }
    
    if (RSA_generate_key_ex(rsa, 2048, bn, NULL) != 1) {
        serve_set_error("failed to generate rsa key");
        goto cleanup;
    }
    
    // create evp key
    EVP_PKEY *key = EVP_PKEY_new();
    if (!key) {
        serve_set_error("failed to create evp key");
        goto cleanup;
    }
    
    if (EVP_PKEY_assign_RSA(key, rsa) != 1) {
        serve_set_error("failed to assign rsa to evp key");
        EVP_PKEY_free(key);
        goto cleanup;
    }
    rsa = NULL; // now owned by key
    
    // create certificate
    X509 *cert = X509_new();
    if (!cert) {
        serve_set_error("failed to create certificate");
        EVP_PKEY_free(key);
        goto cleanup;
    }
    
    // set certificate properties
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 31536000L * days); // days to seconds
    
    X509_set_pubkey(cert, key);
    
    // set subject name
    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char*)"Jubily", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)common_name, -1, -1, 0);
    
    X509_set_issuer_name(cert, name);
    
    // sign certificate
    if (X509_sign(cert, key, EVP_sha256()) == 0) {
        serve_set_error("failed to sign certificate");
        X509_free(cert);
        EVP_PKEY_free(key);
        goto cleanup;
    }
    
    // write certificate to file
    FILE *cert_fp = fopen(cert_file, "w");
    if (!cert_fp) {
        serve_set_error("failed to open certificate file for writing");
        X509_free(cert);
        EVP_PKEY_free(key);
        goto cleanup;
    }
    
    if (PEM_write_X509(cert_fp, cert) != 1) {
        serve_set_error("failed to write certificate");
        fclose(cert_fp);
        X509_free(cert);
        EVP_PKEY_free(key);
        goto cleanup;
    }
    fclose(cert_fp);
    
    // write private key to file
    FILE *key_fp = fopen(key_file, "w");
    if (!key_fp) {
        serve_set_error("failed to open key file for writing");
        X509_free(cert);
        EVP_PKEY_free(key);
        goto cleanup;
    }
    
    if (PEM_write_PrivateKey(key_fp, key, NULL, NULL, 0, NULL, NULL) != 1) {
        serve_set_error("failed to write private key");
        fclose(key_fp);
        X509_free(cert);
        EVP_PKEY_free(key);
        goto cleanup;
    }
    fclose(key_fp);
    
    X509_free(cert);
    EVP_PKEY_free(key);
    BN_free(bn);
    
    SERVE_LOG_INFO("self-signed certificate generated: %s", cert_file);
    return 0;
    
cleanup:
    if (rsa) RSA_free(rsa);
    if (bn) BN_free(bn);
    return -1;
}
