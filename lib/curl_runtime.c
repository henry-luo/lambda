#include "curl_runtime.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdlib.h>

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static CURLcode curl_initialization = CURLE_FAILED_INIT;
static bool curl_keylog_requested;
static void curl_initialize_once(void) {
    const char* keylog = getenv("SSLKEYLOGFILE");
    curl_keylog_requested = keylog && *keylog;
    curl_initialization = curl_global_init(CURL_GLOBAL_DEFAULT);
}
bool curl_runtime_initialize(void) {
    return pthread_once(&curl_once, curl_initialize_once) == 0 && curl_initialization == CURLE_OK;
}
bool curl_runtime_private_tls_ready(void) {
    return curl_runtime_initialize() && !curl_keylog_requested;
}
