#pragma once

#include <curl/curl.h>
#include "enhanced_file_cache.h"

// curl owns this final-response header until the easy handle is cleaned up.
static inline HttpCacheHeaders curl_response_cache_headers(CURL* curl) {
    HttpCacheHeaders headers = {};
    char* content_type = NULL;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
    headers.content_type = content_type ? content_type : "";
    return headers;
}
