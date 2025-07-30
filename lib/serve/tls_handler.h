/**
 * @file tls_handler.h
 * @brief TLS/SSL handling for HTTPS support
 *
 * this file defines functions for managing SSL contexts, loading certificates,
 * and handling secure connections using OpenSSL.
 */

#ifndef SERVE_TLS_HANDLER_H
#define SERVE_TLS_HANDLER_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <event2/bufferevent_ssl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ssl configuration structure
 */
typedef struct {
    char *cert_file;           // path to certificate file
    char *key_file;            // path to private key file
    char *ca_file;             // path to ca certificate file (optional)
    char *ca_path;             // path to ca certificate directory (optional)
    char *cipher_list;         // allowed cipher list (optional)
    int verify_peer;           // whether to verify client certificates
    int verify_depth;          // certificate chain verification depth
    int session_cache_size;    // ssl session cache size
    int session_timeout;       // ssl session timeout in seconds
} tls_config_t;

/**
 * ssl initialization and cleanup
 */

/**
 * initialize ssl library
 * @return 0 on success, -1 on error
 */
int tls_init(void);

/**
 * cleanup ssl library
 */
void tls_cleanup(void);

/**
 * ssl context management
 */

/**
 * create ssl context with given configuration
 * @param config tls configuration
 * @return ssl context or NULL on error
 */
SSL_CTX* tls_create_context(const tls_config_t *config);

/**
 * destroy ssl context
 * @param ctx ssl context
 */
void tls_destroy_context(SSL_CTX *ctx);

/**
 * load certificate and private key into ssl context
 * @param ctx ssl context
 * @param cert_file path to certificate file
 * @param key_file path to private key file
 * @return 0 on success, -1 on error
 */
int tls_load_certificates(SSL_CTX *ctx, const char *cert_file, 
                         const char *key_file);

/**
 * set ca certificates for client verification
 * @param ctx ssl context
 * @param ca_file path to ca certificate file (can be NULL)
 * @param ca_path path to ca certificate directory (can be NULL)
 * @return 0 on success, -1 on error
 */
int tls_set_ca_certificates(SSL_CTX *ctx, const char *ca_file, 
                           const char *ca_path);

/**
 * set cipher list for ssl context
 * @param ctx ssl context
 * @param cipher_list cipher list string
 * @return 0 on success, -1 on error
 */
int tls_set_cipher_list(SSL_CTX *ctx, const char *cipher_list);

/**
 * configure client certificate verification
 * @param ctx ssl context
 * @param verify_peer whether to verify client certificates
 * @param verify_depth maximum certificate chain depth
 */
void tls_set_verify(SSL_CTX *ctx, int verify_peer, int verify_depth);

/**
 * ssl connection management
 */

/**
 * create ssl bufferevent for secure connection
 * @param base event base
 * @param ctx ssl context
 * @param socket socket file descriptor
 * @param state ssl connection state (BUFFEREVENT_SSL_ACCEPTING, etc.)
 * @return ssl bufferevent or NULL on error
 */
struct bufferevent* tls_create_bufferevent(struct event_base *base, 
                                          SSL_CTX *ctx, evutil_socket_t socket,
                                          enum bufferevent_ssl_state state);

/**
 * get ssl object from bufferevent
 * @param bev ssl bufferevent
 * @return ssl object or NULL
 */
SSL* tls_get_ssl(struct bufferevent *bev);

/**
 * get peer certificate from ssl connection
 * @param ssl ssl object
 * @return peer certificate or NULL (must be freed with X509_free)
 */
X509* tls_get_peer_certificate(SSL *ssl);

/**
 * get ssl cipher name
 * @param ssl ssl object
 * @return cipher name or NULL
 */
const char* tls_get_cipher_name(SSL *ssl);

/**
 * get ssl protocol version
 * @param ssl ssl object
 * @return protocol version string or NULL
 */
const char* tls_get_protocol_version(SSL *ssl);

/**
 * ssl error handling
 */

/**
 * get ssl error string
 * @param ssl ssl object (can be NULL)
 * @param ret ssl function return value
 * @return error string
 */
const char* tls_get_error_string(SSL *ssl, int ret);

/**
 * log ssl errors from error queue
 * @param prefix prefix for log messages
 */
void tls_log_errors(const char *prefix);

/**
 * ssl utility functions
 */

/**
 * create default tls configuration
 * @return default configuration
 */
tls_config_t tls_config_default(void);

/**
 * validate tls configuration
 * @param config configuration to validate
 * @return 0 if valid, -1 if invalid
 */
int tls_config_validate(const tls_config_t *config);

/**
 * free resources in tls configuration
 * @param config configuration to clean up
 */
void tls_config_cleanup(tls_config_t *config);

/**
 * check if file is a valid certificate
 * @param cert_file path to certificate file
 * @return 1 if valid, 0 if not
 */
int tls_is_valid_certificate(const char *cert_file);

/**
 * check if file is a valid private key
 * @param key_file path to private key file
 * @return 1 if valid, 0 if not
 */
int tls_is_valid_private_key(const char *key_file);

/**
 * check if certificate and private key match
 * @param cert_file path to certificate file
 * @param key_file path to private key file
 * @return 1 if they match, 0 if not
 */
int tls_certificate_key_match(const char *cert_file, const char *key_file);

/**
 * generate self-signed certificate for testing
 * @param cert_file output certificate file path
 * @param key_file output private key file path
 * @param days validity period in days
 * @param common_name certificate common name
 * @return 0 on success, -1 on error
 */
int tls_generate_self_signed_cert(const char *cert_file, const char *key_file,
                                 int days, const char *common_name);

#ifdef __cplusplus
}
#endif

#endif /* SERVE_TLS_HANDLER_H */
