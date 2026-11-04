#pragma once
#include <curl/curl.h>
#include <mbedtls/ssl.h>
#include "trust_store.h"

static inline CURLcode curl_trust_ssl_ctx(CURL* curl, void* ssl_config, void* user) {
    (void)curl;
    struct mbedtls_x509_crt* roots = user ? (struct mbedtls_x509_crt*)user : trust_store_roots();
    if (roots) mbedtls_ssl_conf_ca_chain((mbedtls_ssl_config*)ssl_config, roots, NULL);
    return CURLE_OK;
}
static inline void curl_use_host_trust_store(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, curl_trust_ssl_ctx);
}
// Explicit roots avoid ambient CA paths and the ordinary profile's override.
static inline CURLcode curl_use_explicit_trust_store(CURL* curl, struct mbedtls_x509_crt* roots) {
    if (!roots) return CURLE_SSL_CACERT_BADFILE;
    CURLcode result = curl_easy_setopt(curl, CURLOPT_CAINFO, (const char*)NULL);
    if (result == CURLE_OK) result = curl_easy_setopt(curl, CURLOPT_CAPATH, (const char*)NULL);
    if (result == CURLE_OK) result = curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, curl_trust_ssl_ctx);
    if (result == CURLE_OK) result = curl_easy_setopt(curl, CURLOPT_SSL_CTX_DATA, roots);
    return result;
}
