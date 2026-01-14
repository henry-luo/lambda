// network_downloader.cpp
// Network download implementation using libcurl

#include "network_downloader.h"
#include "enhanced_file_cache.h"
#include "../../lib/log.h"
#include "../../lib/file_utils.h"
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Response data structure
typedef struct {
    char* data;
    size_t size;
} HttpResponse;

// Callback for curl to write response data
static size_t write_response_callback(void* contents, size_t size, size_t nmemb, HttpResponse* response) {
    size_t total_size = size * nmemb;
    char* new_data = (char*)realloc(response->data, response->size + total_size + 1);
    
    if (!new_data) {
        log_error("network: memory allocation failed during download");
        return 0;
    }
    
    response->data = new_data;
    memcpy(&(response->data[response->size]), contents, total_size);
    response->size += total_size;
    response->data[response->size] = '\0';
    
    return total_size;
}

// Initialize curl (one-time setup)
static bool curl_initialized = false;

static bool init_curl() {
    if (!curl_initialized) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            log_error("network: failed to initialize libcurl");
            return false;
        }
        curl_initialized = true;
    }
    return true;
}

// Check if HTTP error is retryable
bool is_http_error_retryable(long http_code) {
    // 5xx errors (server errors) are retryable
    if (http_code >= 500 && http_code < 600) {
        return true;
    }
    
    // 4xx errors (client errors) are not retryable
    if (http_code >= 400 && http_code < 500) {
        return false;
    }
    
    // Other errors (timeout, connection refused, etc.) are retryable
    return true;
}

// Download network resource
bool network_download_resource(NetworkResource* res) {
    if (!res || !res->url) {
        log_error("network: invalid resource for download");
        return false;
    }
    
    if (!init_curl()) {
        if (res->error_message) free(res->error_message);
        res->error_message = strdup("Failed to initialize libcurl");
        return false;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("network: failed to create curl handle");
        if (res->error_message) free(res->error_message);
        res->error_message = strdup("Failed to create curl handle");
        return false;
    }
    
    HttpResponse response = {0};
    CURLcode curl_res;
    
    // Configure curl options
    curl_easy_setopt(curl, CURLOPT_URL, res->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Timeout configuration (milliseconds)
    int timeout_ms = res->timeout_ms > 0 ? res->timeout_ms : 30000;  // Default 30s
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);  // 5s connection timeout
    
    // Redirects and user agent
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Radiant/1.0 Lambda-Script");
    
    // SSL verification (always enabled for production)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Compression support
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    
    // Perform the download
    log_debug("network: downloading %s (timeout: %dms)", res->url, timeout_ms);
    curl_res = curl_easy_perform(curl);
    
    // Check for curl errors
    if (curl_res != CURLE_OK) {
        const char* error_str = curl_easy_strerror(curl_res);
        log_error("network: download failed for %s: %s", res->url, error_str);
        
        if (res->error_message) free(res->error_message);
        res->error_message = strdup(error_str);
        
        free(response.data);
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Check HTTP response code
    long http_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    res->http_status_code = (int)http_code;
    
    if (http_code >= 400) {
        log_error("network: HTTP %ld for %s", http_code, res->url);
        
        // Set appropriate error message
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "HTTP %ld", http_code);
        if (res->error_message) free(res->error_message);
        res->error_message = strdup(error_msg);
        
        free(response.data);
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Get actual URL after redirects
    char* final_url = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &final_url);
    
    log_debug("network: successfully downloaded %zu bytes from %s (HTTP %ld)",
              response.size, res->url, http_code);
    
    // Store in cache (enhanced_file_cache will handle file writing)
    if (res->cache) {
        char* cached_path = enhanced_cache_store(res->cache, res->url, 
                                                  response.data, response.size, NULL);
        if (cached_path) {
            if (res->local_path) free(res->local_path);
            res->local_path = cached_path;
        }
    }
    
    // Save local path for resource handlers
    // For now, we'll write to a temporary file if cache doesn't provide path
    if (!res->local_path) {
        // Generate a temporary filename
        char temp_path[512];
        snprintf(temp_path, sizeof(temp_path), "./temp/download_%p.tmp", (void*)res);
        
        // Write content to file
        FILE* f = fopen(temp_path, "wb");
        if (f) {
            fwrite(response.data, 1, response.size, f);
            fclose(f);
            res->local_path = strdup(temp_path);
            log_debug("network: saved to temporary file: %s", temp_path);
        } else {
            log_error("network: failed to write temporary file: %s", temp_path);
        }
    }
    
    free(response.data);
    curl_easy_cleanup(curl);
    
    return true;
}
