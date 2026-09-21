// input_http.cpp
// HTTP/HTTPS handling for Lambda Script input system using libcurl
// Handles downloading files to cache and returning Input*

#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include "../../lib/mem.h"
#include "../../lib/byte_builder.h"
#include "input.hpp"
#include "../network/http_client.h"
#include "../network/enhanced_file_cache.h"
#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/string.h"
#include "../../lib/mime-detect.h"

// Structure to hold response data
typedef struct {
    ByteBuilder body;
} HttpResponse;

// HttpConfig is now defined in input.h

// Default HTTP configuration
static HttpConfig default_http_config = {
    .timeout_seconds = 30,
    .max_redirects = 5,
    .user_agent = RADIANT_HTTP_CLIENT_USER_AGENT,
    .verify_ssl = true,
    .enable_compression = true
};

// helper: configure the optional request body for methods that carry one
static void http_set_request_body(CURL* curl, const FetchConfig* config) {
    if (!config || !config->body) return;
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, config->body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, config->body_size);
}

// Maximum response size (50 MB) — prevents unbounded memory growth from large pages
#define HTTP_MAX_RESPONSE_SIZE (50 * 1024 * 1024)
#define HTTP_CACHE_MAX_SIZE (100 * 1024 * 1024)
#define HTTP_CACHE_MAX_ENTRIES 10000

// Callback function to write response data
static size_t write_response_callback(void* contents, size_t size, size_t nmemb, HttpResponse* response) {
    size_t total_size = size * nmemb;
    if (!response || !byte_builder_append_limited(&response->body, contents,
                                                   total_size, HTTP_MAX_RESPONSE_SIZE)) {
        log_error("HTTP: Response exceeds maximum size (%d MB), aborting download",
                  HTTP_MAX_RESPONSE_SIZE / (1024 * 1024));
        return 0;  // returning 0 causes curl to abort with CURLE_WRITE_ERROR
    }
    return total_size;
}

// Initialize libcurl (call once at startup)
static bool curl_initialized = false;

static bool init_curl() {
    if (!curl_initialized) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            log_error("HTTP: Failed to initialize libcurl");
            return false;
        }
        curl_initialized = true;
    }
    return true;
}

// Map HTTP Content-Type to file extension for routing
// Delegates to lib/mime-detect; defaults to ".html" for unknown types
const char* content_type_to_extension(const char* content_type) {
    const char* ext = mime_extension_from_content_type(content_type);
    if (ext) return ext;
    if (content_type) {
        log_debug("HTTP: Unknown content-type '%s', defaulting to .html", content_type);
    }
    return ".html";
}

// Download HTTP/HTTPS resource and return content in memory
char* download_http_content(const char* url, size_t* content_size, const HttpConfig* config, char** effective_url) {
    if (!init_curl()) {
        return NULL;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("HTTP: Failed to initialize curl handle");
        return NULL;
    }

    HttpResponse response = {0};
    if (!byte_builder_init(&response.body, 0, MEM_CAT_TEMP, true)) {
        curl_easy_cleanup(curl);
        return NULL;
    }
    CURLcode res;

    // Configure curl options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config ? config->timeout_seconds : default_http_config.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config ? config->max_redirects : default_http_config.max_redirects);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config ? config->user_agent : default_http_config.user_agent);

    // SSL/TLS configuration
    if (config ? config->verify_ssl : default_http_config.verify_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    // Compression support
    if (config ? config->enable_compression : default_http_config.enable_compression) {
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    }

    // Prefer HTTP/2 over HTTPS (falls back to HTTP/1.1 if unsupported)
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);

    // Perform the request
    log_debug("HTTP: Downloading %s\n", url);
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        // Provide specific error messages for common failure modes
        switch (res) {
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
            case CURLE_SSL_CIPHER:
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
                log_error("HTTP: SSL/TLS certificate error for %s: %s", url, curl_easy_strerror(res));
                break;
            case CURLE_TOO_MANY_REDIRECTS:
                log_error("HTTP: Redirect loop detected for %s (max %ld redirects exceeded)",
                          url, config ? config->max_redirects : default_http_config.max_redirects);
                break;
            case CURLE_OPERATION_TIMEDOUT:
                log_error("HTTP: Request timed out for %s (limit %lds)",
                          url, config ? config->timeout_seconds : default_http_config.timeout_seconds);
                break;
            case CURLE_COULDNT_RESOLVE_HOST:
                log_error("HTTP: Could not resolve host for %s", url);
                break;
            case CURLE_COULDNT_CONNECT:
                log_error("HTTP: Connection refused for %s", url);
                break;
            default:
                log_error("HTTP: Download failed for %s: %s", url, curl_easy_strerror(res));
                break;
        }
        byte_builder_destroy(&response.body);
        curl_easy_cleanup(curl);
        return NULL;
    }

    // Check HTTP response code
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code >= 400) {
        log_error("HTTP: Server returned error %ld for %s", response_code, url);
        byte_builder_destroy(&response.body);
        curl_easy_cleanup(curl);
        return NULL;
    }

    log_debug("HTTP: Successfully downloaded %zu bytes from %s (HTTP %ld)\n",
              response.body.length, url, response_code);

    // Capture effective URL after redirects (may differ from original url)
    if (effective_url) {
        char* eff_url = NULL;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
        if (eff_url && strcmp(eff_url, url) != 0) {
            *effective_url = mem_strdup(eff_url, MEM_CAT_TEMP);
            log_debug("HTTP: Effective URL after redirect: %s", *effective_url);
        } else {
            *effective_url = NULL;
        }
    }

    if (content_size) {
        *content_size = response.body.length;
    }

    curl_easy_cleanup(curl);
    return (char*)byte_builder_take(&response.body, NULL);
}

// Keep synchronous consumers on the same durable SHA-256 cache format as the
// resource manager. A fresh cache object restores an existing native entry.
static char* download_http_content_with_enhanced_cache(const char* url,
                                                       size_t* content_size,
                                                       const char* cache_dir,
                                                       char** out_cache_path,
                                                       bool require_cache_path) {
    if (content_size) *content_size = 0;
    if (out_cache_path) *out_cache_path = NULL;
    if (!url || !cache_dir) return NULL;

    EnhancedFileCache* cache = enhanced_cache_create(
        cache_dir, HTTP_CACHE_MAX_SIZE, HTTP_CACHE_MAX_ENTRIES);
    if (!cache) {
        log_debug("HTTP: enhanced cache unavailable for %s", url);
        return require_cache_path ? NULL : download_http_content(url, content_size, NULL);
    }

    char* cache_path = enhanced_cache_lookup(cache, url);
    if (cache_path) {
        size_t cached_size = 0;
        char* content = read_binary_file(cache_path, &cached_size);
        if (content) {
            log_debug("HTTP: native cache hit for %s (%zu bytes)", url, cached_size);
            if (content_size) *content_size = cached_size;
            enhanced_cache_destroy(cache);
            if (out_cache_path) *out_cache_path = cache_path;
            else mem_free(cache_path);
            return content;
        }
        mem_free(cache_path);
        cache_path = NULL;
    }

    size_t downloaded_size = 0;
    char* content = download_http_content(url, &downloaded_size, NULL);
    if (content) {
        cache_path = enhanced_cache_store(cache, url, content, downloaded_size, NULL);
        if (cache_path) {
            log_debug("HTTP: stored native cache entry for %s", url);
        } else if (require_cache_path) {
            // Callers such as the package installer require a durable path.
            mem_free(content);
            content = NULL;
        }
    }

    enhanced_cache_destroy(cache);
    if (content_size && content) *content_size = downloaded_size;
    if (out_cache_path) *out_cache_path = cache_path;
    else mem_free(cache_path);
    return content;
}

// Download HTTP/HTTPS resource to the native cache and optionally return its path.
char* download_to_cache(const char* url, const char* cache_dir, char** out_cache_path) {
    return download_http_content_with_enhanced_cache(
        url, NULL, cache_dir, out_cache_path, out_cache_path != NULL);
}

// Cache-aware synchronous download (returns content + size). Checks disk cache first.
char* download_http_content_cached(const char* url, size_t* content_size, const char* cache_dir) {
    const char* effective_cache_dir = cache_dir ? cache_dir : "./temp/cache";
    return download_http_content_with_enhanced_cache(
        url, content_size, effective_cache_dir, NULL, false);
}

// Returns an Input* for HTTP/HTTPS URL, using memory and file cache
Input* input_from_http_with_name_parent(const char* url, const char* type,
        const char* flavor, const char* cache_dir, NamePool* name_parent) {
    if (!url) {
        return NULL;
    }

    // Use default cache directory if none provided
    const char* effective_cache_dir = cache_dir ? cache_dir : "./temp/cache";

    // Download content (with caching)
    char* cache_path = NULL;
    char* content = download_to_cache(url, effective_cache_dir, &cache_path);

    if (!content) {
        return NULL;
    }

    // Parse URL to create Url object
    Url* abs_url = url_parse(url);
    if (!abs_url) {
        mem_free(content); // from lib - uses mem_alloc
        mem_free(cache_path); // from lib - uses mem_alloc
        return NULL;
    }

    // Create type and flavor strings
    String* type_str = NULL;
    String* flavor_str = NULL;

    if (type) {
        type_str = string_from_strview_mem(strview_from_cstr(type), MEM_CAT_INPUT_OTHER);
    }

    if (flavor) {
        flavor_str = string_from_strview_mem(strview_from_cstr(flavor), MEM_CAT_INPUT_OTHER);
    }

    // Parse content using existing input system
    Input* input = input_from_source_with_name_parent(content, abs_url,
        type_str, flavor_str, name_parent);

    // Cleanup
    mem_free(content); // from lib - uses mem_alloc
    mem_free(cache_path); // from lib - uses mem_alloc
    mem_free(type_str);
    mem_free(flavor_str);
    return input;
}

Input* input_from_http(const char* url, const char* type, const char* flavor,
        const char* cache_dir) {
    return input_from_http_with_name_parent(url, type, flavor, cache_dir, NULL);
}

// Extended HTTP configuration for fetch operations
// Note: FetchConfig and FetchResponse are defined in input.h

// Callback to collect response headers
static size_t header_callback(char* buffer, size_t size, size_t nitems, FetchResponse* response) {
    size_t header_size = size * nitems;

    // Skip status line and empty lines
    if (header_size < 3 || buffer[0] == '\r' || buffer[0] == '\n') {
        return header_size;
    }

    // Extract Content-Type header
    if (str_istarts_with_const(buffer, header_size, "content-type:")) {
        const char* value_start = buffer + 13;
        value_start = str_skip_line_space(value_start);

        size_t value_len = header_size - (value_start - buffer);
        // Remove trailing CRLF
        while (value_len > 0 && (value_start[value_len-1] == '\r' || value_start[value_len-1] == '\n')) {
            value_len--;
        }

        if (response->content_type) mem_free(response->content_type);
        response->content_type = mem_dup_n(value_start, value_len, MEM_CAT_INPUT_OTHER);
    }

    // Store all headers
    response->response_headers = (char**)mem_realloc(response->response_headers,
                                               (response->response_header_count + 1) * sizeof(char*), MEM_CAT_INPUT_OTHER);
    if (response->response_headers) {
        char* header_copy = mem_dup_n(buffer, header_size, MEM_CAT_INPUT_OTHER);
        if (header_copy) {
            // Remove trailing CRLF
            size_t len = header_size;
            while (len > 0 && (header_copy[len-1] == '\r' || header_copy[len-1] == '\n')) {
                header_copy[--len] = '\0';
            }

            response->response_headers[response->response_header_count++] = header_copy;
        }
    }

    return header_size;
}

// Free FetchResponse structure
void free_fetch_response(FetchResponse* response) {
    if (!response) return;
    if (response->data) {
        mem_free(response->data);
        response->data = NULL;
    }

    if (response->content_type) {
        mem_free(response->content_type);
        response->content_type = NULL;
    }

    for (int i = 0; i < response->response_header_count; i++) {
        mem_free(response->response_headers[i]);
    }
    if (response->response_headers) {
        mem_free(response->response_headers);
        response->response_headers = NULL;
    }
    response->response_header_count = 0;

    if (response->effective_url) {
        mem_free(response->effective_url);
        response->effective_url = NULL;
    }

    mem_free(response);
}

// Perform HTTP request with full fetch-like functionality
FetchResponse* http_fetch(const char* url, const FetchConfig* config) {
    if (!init_curl()) { return NULL; }

    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("HTTP: Failed to initialize curl handle\n");
        return NULL;
    }

    FetchResponse* response = (FetchResponse*)mem_calloc(1, sizeof(FetchResponse), MEM_CAT_INPUT_OTHER);
    if (!response) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    CURLcode res;

    // Basic configuration
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, response);

    // Prefer HTTP/2 over HTTPS (falls back to HTTP/1.1 if unsupported)
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);

    // Method configuration
    if (config && config->method) {
        size_t method_len = strlen(config->method);
        if (str_ieq_const(config->method, method_len, "POST")) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            http_set_request_body(curl, config);
        } else if (str_ieq_const(config->method, method_len, "PUT")) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            http_set_request_body(curl, config);
        } else if (str_ieq_const(config->method, method_len, "DELETE")) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (str_ieq_const(config->method, method_len, "PATCH")) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            http_set_request_body(curl, config);
        } else if (str_ieq_const(config->method, method_len, "HEAD")) {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        }
        // GET is default, no special handling needed
    }

    // Headers configuration
    struct curl_slist* headers = NULL;
    if (config && config->headers && config->header_count > 0) {
        for (int i = 0; i < config->header_count; i++) {
            headers = curl_slist_append(headers, config->headers[i]);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    // Other configuration
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config ? config->timeout_seconds : default_http_config.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config ? config->max_redirects : default_http_config.max_redirects);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config ? config->user_agent : default_http_config.user_agent);

    // SSL/TLS configuration
    bool verify_ssl = config ? config->verify_ssl : default_http_config.verify_ssl;
    if (verify_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    // Compression support
    bool enable_compression = config ? config->enable_compression : default_http_config.enable_compression;
    if (enable_compression) {
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    }

    // Perform the request
    log_debug("HTTP: Fetching %s\n", url);
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_error("HTTP: Fetch failed: %s\n", curl_easy_strerror(res));
        free_fetch_response(response);
        curl_easy_cleanup(curl);
        if (headers) curl_slist_free_all(headers);
        return NULL;
    }

    // Get HTTP response code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status_code);

    // Capture effective URL after redirects
    char* eff_url = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
    if (eff_url && strcmp(eff_url, url) != 0) {
        response->effective_url = mem_strdup(eff_url, MEM_CAT_TEMP);
        log_debug("HTTP: Effective URL after redirect: %s", response->effective_url);
    } else {
        response->effective_url = NULL;
    }

    log_debug("HTTP: Successfully fetched %zu bytes from %s (HTTP %ld)\n",
           response->size, url, response->status_code);

    // Cleanup
    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);

    return response;
}
