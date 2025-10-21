/**
 * @file mbedtls_compat.h
 * @brief Compatibility layer for mbedTLS to provide OpenSSL-like API
 *
 * this file provides wrapper macros and types to map OpenSSL-style API
 * calls to mbedTLS equivalents, making migration easier.
 */

#ifndef SERVE_MBEDTLS_COMPAT_H
#define SERVE_MBEDTLS_COMPAT_H

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>
#include <mbedtls/net_sockets.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ssl context wrapper that holds mbedtls state
 */
typedef struct {
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context pkey;
    mbedtls_x509_crt ca_chain;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    int initialized;
} mbedtls_ssl_ctx_t;

/**
 * ssl connection wrapper
 */
typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_ctx_t *ctx;
    int socket_fd;
} mbedtls_ssl_conn_t;

/**
 * type aliases for compatibility
 */
#define SSL_CTX mbedtls_ssl_ctx_t
#define SSL mbedtls_ssl_conn_t
#define X509 mbedtls_x509_crt

/**
 * ssl/tls method selection
 */
#define TLS_server_method() (1)
#define TLS_client_method() (0)

/**
 * ssl options flags (mapped to mbedtls equivalents where applicable)
 */
#define SSL_OP_NO_SSLv2                 0x00000001L
#define SSL_OP_NO_SSLv3                 0x00000002L
#define SSL_OP_NO_COMPRESSION           0x00000004L
#define SSL_OP_SINGLE_DH_USE            0x00000008L
#define SSL_OP_SINGLE_ECDH_USE          0x00000010L
#define SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION 0x00000020L

/**
 * ssl verification modes
 */
#define SSL_VERIFY_NONE                 0
#define SSL_VERIFY_PEER                 1
#define SSL_VERIFY_FAIL_IF_NO_PEER_CERT 2

/**
 * ssl file types
 */
#define SSL_FILETYPE_PEM    1
#define SSL_FILETYPE_ASN1   2

/**
 * ssl session cache modes
 */
#define SSL_SESS_CACHE_OFF      0x0000
#define SSL_SESS_CACHE_CLIENT   0x0001
#define SSL_SESS_CACHE_SERVER   0x0002
#define SSL_SESS_CACHE_BOTH     (SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_SERVER)

/**
 * protocol versions
 */
#define TLS1_2_VERSION  0x0303
#define TLS1_3_VERSION  0x0304

/**
 * ssl error codes
 */
#define SSL_ERROR_NONE                      0
#define SSL_ERROR_SSL                       1
#define SSL_ERROR_WANT_READ                 2
#define SSL_ERROR_WANT_WRITE                3
#define SSL_ERROR_WANT_X509_LOOKUP          4
#define SSL_ERROR_SYSCALL                   5
#define SSL_ERROR_ZERO_RETURN               6
#define SSL_ERROR_WANT_CONNECT              7
#define SSL_ERROR_WANT_ACCEPT               8

/**
 * helper functions to implement openssl-like api using mbedtls
 */

/**
 * initialize ssl library (global init)
 */
int mbedtls_compat_library_init(void);

/**
 * cleanup ssl library
 */
void mbedtls_compat_library_cleanup(void);

/**
 * create new ssl context
 */
SSL_CTX* mbedtls_compat_SSL_CTX_new(int is_server);

/**
 * free ssl context
 */
void mbedtls_compat_SSL_CTX_free(SSL_CTX *ctx);

/**
 * set ssl options
 */
long mbedtls_compat_SSL_CTX_set_options(SSL_CTX *ctx, long options);

/**
 * set minimum protocol version
 */
int mbedtls_compat_SSL_CTX_set_min_proto_version(SSL_CTX *ctx, int version);

/**
 * load certificate file
 */
int mbedtls_compat_SSL_CTX_use_certificate_file(SSL_CTX *ctx,
                                                const char *file,
                                                int type);

/**
 * load private key file
 */
int mbedtls_compat_SSL_CTX_use_PrivateKey_file(SSL_CTX *ctx,
                                               const char *file,
                                               int type);

/**
 * check if private key matches certificate
 */
int mbedtls_compat_SSL_CTX_check_private_key(SSL_CTX *ctx);

/**
 * load ca certificates
 */
int mbedtls_compat_SSL_CTX_load_verify_locations(SSL_CTX *ctx,
                                                 const char *ca_file,
                                                 const char *ca_path);

/**
 * set cipher list
 */
int mbedtls_compat_SSL_CTX_set_cipher_list(SSL_CTX *ctx, const char *list);

/**
 * set verification mode and callback
 */
void mbedtls_compat_SSL_CTX_set_verify(SSL_CTX *ctx, int mode,
                                       void *callback);

/**
 * set verification depth
 */
void mbedtls_compat_SSL_CTX_set_verify_depth(SSL_CTX *ctx, int depth);

/**
 * set session cache mode
 */
long mbedtls_compat_SSL_CTX_set_session_cache_mode(SSL_CTX *ctx, long mode);

/**
 * set session cache size
 */
long mbedtls_compat_SSL_CTX_sess_set_cache_size(SSL_CTX *ctx, long size);

/**
 * set session timeout
 */
long mbedtls_compat_SSL_CTX_set_timeout(SSL_CTX *ctx, long timeout);

/**
 * create new ssl connection
 */
SSL* mbedtls_compat_SSL_new(SSL_CTX *ctx);

/**
 * free ssl connection
 */
void mbedtls_compat_SSL_free(SSL *ssl);

/**
 * set socket for ssl connection
 */
int mbedtls_compat_SSL_set_fd(SSL *ssl, int fd);

/**
 * perform ssl handshake (server)
 */
int mbedtls_compat_SSL_accept(SSL *ssl);

/**
 * perform ssl handshake (client)
 */
int mbedtls_compat_SSL_connect(SSL *ssl);

/**
 * read data from ssl connection
 */
int mbedtls_compat_SSL_read(SSL *ssl, void *buf, int num);

/**
 * write data to ssl connection
 */
int mbedtls_compat_SSL_write(SSL *ssl, const void *buf, int num);

/**
 * shutdown ssl connection
 */
int mbedtls_compat_SSL_shutdown(SSL *ssl);

/**
 * get ssl error code
 */
int mbedtls_compat_SSL_get_error(SSL *ssl, int ret);

/**
 * get peer certificate
 */
X509* mbedtls_compat_SSL_get_peer_certificate(SSL *ssl);

/**
 * get cipher name
 */
const char* mbedtls_compat_SSL_get_cipher_name(SSL *ssl);

/**
 * get protocol version string
 */
const char* mbedtls_compat_SSL_get_version(SSL *ssl);

/**
 * free x509 certificate
 */
void mbedtls_compat_X509_free(X509 *cert);

/**
 * get error string
 */
const char* mbedtls_compat_ERR_error_string(unsigned long error, char *buf);

/**
 * get error from queue
 */
unsigned long mbedtls_compat_ERR_get_error(void);

/**
 * error string with buffer size
 */
void mbedtls_compat_ERR_error_string_n(unsigned long error,
                                       char *buf,
                                       size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SERVE_MBEDTLS_COMPAT_H */
