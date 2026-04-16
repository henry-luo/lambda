// network_downloader.cpp
// Network download implementation using libcurl

#include "network_downloader.h"
#include "network_resource_manager.h"
#include "cookie_jar.h"
#include "enhanced_file_cache.h"
#include "../../lib/log.h"
#include "../../lib/file_utils.h"
#include <curl/curl.h>
#include <string.h>
#include "../../lib/mem.h"
#include <time.h>
#include <pthread.h>

// Shared connection pool for connection reuse across threads
static CURLSH* shared_handle = NULL;
static pthread_mutex_t share_lock_conn = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t share_lock_dns  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t share_lock_ssl  = PTHREAD_MUTEX_INITIALIZER;

static void share_lock_cb(CURL* handle, curl_lock_data data,
                          curl_lock_access access, void* userptr) {
    (void)handle; (void)access; (void)userptr;
    switch (data) {
        case CURL_LOCK_DATA_CONNECT: pthread_mutex_lock(&share_lock_conn); break;
        case CURL_LOCK_DATA_DNS:     pthread_mutex_lock(&share_lock_dns);  break;
        case CURL_LOCK_DATA_SSL_SESSION: pthread_mutex_lock(&share_lock_ssl); break;
        default: break;
    }
}

static void share_unlock_cb(CURL* handle, curl_lock_data data, void* userptr) {
    (void)handle; (void)userptr;
    switch (data) {
        case CURL_LOCK_DATA_CONNECT: pthread_mutex_unlock(&share_lock_conn); break;
        case CURL_LOCK_DATA_DNS:     pthread_mutex_unlock(&share_lock_dns);  break;
        case CURL_LOCK_DATA_SSL_SESSION: pthread_mutex_unlock(&share_lock_ssl); break;
        default: break;
    }
}

void network_downloader_init_shared(void) {
    if (shared_handle) return;
    shared_handle = curl_share_init();
    if (shared_handle) {
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(shared_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        curl_share_setopt(shared_handle, CURLSHOPT_LOCKFUNC, share_lock_cb);
        curl_share_setopt(shared_handle, CURLSHOPT_UNLOCKFUNC, share_unlock_cb);
        log_debug("network: shared connection pool initialized");
    }
}

void network_downloader_cleanup_shared(void) {
    if (shared_handle) {
        curl_share_cleanup(shared_handle);
        shared_handle = NULL;
        log_debug("network: shared connection pool cleaned up");
    }
}

// Response data structure
typedef struct {
    char* data;
    size_t size;
} HttpResponse;

// Maximum single resource size (100 MB) — prevents unbounded memory growth
#define NETWORK_MAX_RESOURCE_SIZE (100 * 1024 * 1024)

// Callback for curl to write response data
static size_t write_response_callback(void* contents, size_t size, size_t nmemb, HttpResponse* response) {
    size_t total_size = size * nmemb;
    
    // enforce maximum resource size
    if (response->size + total_size > NETWORK_MAX_RESOURCE_SIZE) {
        log_error("network: resource exceeds maximum size (%d MB), aborting download",
                  NETWORK_MAX_RESOURCE_SIZE / (1024 * 1024));
        return 0;  // returning 0 causes curl to abort with CURLE_WRITE_ERROR
    }
    
    char* new_data = (char*)mem_realloc(response->data, response->size + total_size + 1, MEM_CAT_NETWORK);
    
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

// Context for header callback (captures Set-Cookie headers for cookie jar)
typedef struct {
    CookieJar* jar;
    const char* request_url;
} HeaderCallbackCtx;

// Callback for curl to receive response headers — captures Set-Cookie
static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    HeaderCallbackCtx* ctx = (HeaderCallbackCtx*)userdata;
    if (!ctx || !ctx->jar) return total;

    // check for "Set-Cookie:" prefix (case-insensitive)
    if (total > 12 && strncasecmp(buffer, "Set-Cookie:", 11) == 0) {
        // make null-terminated copy (strip trailing \r\n)
        size_t len = total;
        while (len > 0 && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n'))
            len--;
        char* header_str = (char*)mem_alloc(len + 1, MEM_CAT_NETWORK);
        memcpy(header_str, buffer, len);
        header_str[len] = '\0';

        cookie_jar_store(ctx->jar, ctx->request_url, header_str);
        mem_free(header_str);
    }
    return total;
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
        if (res->error_message) mem_free(res->error_message);
        res->error_message = mem_strdup("Failed to initialize libcurl", MEM_CAT_NETWORK);
        return false;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("network: failed to create curl handle");
        if (res->error_message) mem_free(res->error_message);
        res->error_message = mem_strdup("Failed to create curl handle", MEM_CAT_NETWORK);
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
    
    // Prefer HTTP/2 over HTTPS (falls back to HTTP/1.1 if unsupported)
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    
    // per-host connection limit: 6 for HTTP/1.1 (matches browser behavior),
    // HTTP/2 multiplexes over 1 connection automatically
    curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, 6L);
    
    // Share connection pool, DNS cache, and SSL sessions across threads
    if (shared_handle) {
        curl_easy_setopt(curl, CURLOPT_SHARE, shared_handle);
    }
    
    // Phase 4: Cookie integration
    struct curl_slist* custom_headers = NULL;
    HeaderCallbackCtx header_ctx = {NULL, NULL};
    CookieJar* jar = (res->manager) ? res->manager->cookie_jar : NULL;
    
    if (jar) {
        // inject Cookie header from jar
        bool is_secure = (strncasecmp(res->url, "https://", 8) == 0);
        char* cookie_value = cookie_jar_build_request_header(jar, res->url, is_secure);
        if (cookie_value) {
            // build "Cookie: name=val; name2=val2" header
            size_t hdr_len = strlen(cookie_value) + 9;  // "Cookie: " + val + nul
            char* cookie_hdr = (char*)mem_alloc(hdr_len, MEM_CAT_NETWORK);
            snprintf(cookie_hdr, hdr_len, "Cookie: %s", cookie_value);
            custom_headers = curl_slist_append(custom_headers, cookie_hdr);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, custom_headers);
            mem_free(cookie_hdr);
            mem_free(cookie_value);
        }

        // set up header callback to capture Set-Cookie responses
        header_ctx.jar = jar;
        header_ctx.request_url = res->url;
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_ctx);
    }
    
    // Perform the download
    log_debug("network: downloading %s (timeout: %dms)", res->url, timeout_ms);
    curl_res = curl_easy_perform(curl);
    
    // Check for curl errors
    if (curl_res != CURLE_OK) {
        const char* error_str = curl_easy_strerror(curl_res);
        
        // Provide specific error messages for common failure modes
        char error_msg[256];
        switch (curl_res) {
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
            case CURLE_SSL_CIPHER:
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
                snprintf(error_msg, sizeof(error_msg), "SSL certificate error: %s", error_str);
                log_error("network: SSL/TLS certificate error for %s: %s", res->url, error_str);
                break;
            case CURLE_TOO_MANY_REDIRECTS:
                snprintf(error_msg, sizeof(error_msg), "Redirect loop (max 5 redirects exceeded)");
                log_error("network: redirect loop detected for %s", res->url);
                break;
            case CURLE_OPERATION_TIMEDOUT:
                snprintf(error_msg, sizeof(error_msg), "Request timed out");
                log_error("network: request timed out for %s", res->url);
                break;
            case CURLE_COULDNT_RESOLVE_HOST:
                snprintf(error_msg, sizeof(error_msg), "Could not resolve host");
                log_error("network: could not resolve host for %s", res->url);
                break;
            case CURLE_COULDNT_CONNECT:
                snprintf(error_msg, sizeof(error_msg), "Connection refused");
                log_error("network: connection refused for %s", res->url);
                break;
            default:
                snprintf(error_msg, sizeof(error_msg), "%s", error_str);
                log_error("network: download failed for %s: %s", res->url, error_str);
                break;
        }
        
        if (res->error_message) mem_free(res->error_message);
        res->error_message = mem_strdup(error_msg, MEM_CAT_NETWORK);
        
        mem_free(response.data);
        if (custom_headers) curl_slist_free_all(custom_headers);
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
        if (res->error_message) mem_free(res->error_message);
        res->error_message = mem_strdup(error_msg, MEM_CAT_NETWORK);
        
        mem_free(response.data);
        if (custom_headers) curl_slist_free_all(custom_headers);
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
            if (res->local_path) mem_free(res->local_path);
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
            res->local_path = mem_strdup(temp_path, MEM_CAT_NETWORK);
            log_debug("network: saved to temporary file: %s", temp_path);
        } else {
            log_error("network: failed to write temporary file: %s", temp_path);
        }
    }
    
    mem_free(response.data);
    if (custom_headers) curl_slist_free_all(custom_headers);
    curl_easy_cleanup(curl);
    
    return true;
}
