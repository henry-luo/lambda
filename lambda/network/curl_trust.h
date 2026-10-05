#pragma once

// HTTPS trusts the host's one certificate store (lib/trust_store): the same
// roots as the RDB TLS bridge, read from the OS at run time instead of the
// CA path compiled into curl (vibe/Lambda_IO_RDB.md section 13.9).

#include <curl/curl.h>
#include <mbedtls/ssl.h>
#include "../../lib/trust_store.h"

/**
 * curl's mbedTLS backend calls this after its own setup, for TLS connections
 * only: plain HTTP never loads the store, and every connection shares the one
 * parsed chain instead of re-parsing a bundle.
 */
static inline CURLcode curl_trust_ssl_ctx(CURL* curl, void* ssl_config, void* user) {
    (void)curl; (void)user;
    struct mbedtls_x509_crt* roots = trust_store_roots();
    // with no OS roots at all, curl keeps its compiled-in bundle
    if (roots) mbedtls_ssl_conf_ca_chain((mbedtls_ssl_config*)ssl_config, roots, NULL);
    return CURLE_OK;
}

static inline void curl_use_host_trust_store(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, curl_trust_ssl_ctx);
}
