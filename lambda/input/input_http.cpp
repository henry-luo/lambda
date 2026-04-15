// input_http.cpp
// HTTP/HTTPS handling for Lambda Script input system using libcurl
// Handles downloading files to cache and returning Input*

#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include "../../lib/mem.h"
#include "input.hpp"
#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/mime-detect.h"

// Structure to hold response data
typedef struct {
    char* data;
    size_t size;
} HttpResponse;

// HttpConfig is now defined in input.h

// Default HTTP configuration
static HttpConfig default_http_config = {
    .timeout_seconds = 30,
    .max_redirects = 5,
    .user_agent = "Lambda-Script/1.0",
    .verify_ssl = true,
    .enable_compression = true
};

// Callback function to write response data
static size_t write_response_callback(void* contents, size_t size, size_t nmemb, HttpResponse* response) {
    size_t total_size = size * nmemb;
    char* new_data = (char*)mem_realloc(response->data, response->size + total_size + 1, MEM_CAT_TEMP);  // tracked realloc: callers use mem_free()

    if (!new_data) {
        log_error("HTTP: Memory allocation failed");
        return 0;
    }

    response->data = new_data;
    memcpy(&(response->data[response->size]), contents, total_size);
    response->size += total_size;
    response->data[response->size] = '\0';

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
static const char* content_type_to_extension(const char* content_type) {
    const char* ext = mime_extension_from_content_type(content_type);
    if (ext) return ext;
    if (content_type) {
        log_debug("HTTP: Unknown content-type '%s', defaulting to .html", content_type);
    }
    return ".html";
}

// Generate cache filename from URL — delegates to lib/file
static char* generate_cache_filename(const char* url, const char* cache_dir) {
    return file_cache_path(url, cache_dir, ".cache");
}

// Download HTTP/HTTPS resource and return content in memory
char* download_http_content(const char* url, size_t* content_size, const HttpConfig* config) {
    if (!init_curl()) {
        return NULL;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("HTTP: Failed to initialize curl handle");
        return NULL;
    }

    HttpResponse response = {0};
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
        log_error("HTTP: Download failed: %s", curl_easy_strerror(res));
        mem_free(response.data);  // tracked free: matches mem_realloc in write_response_callback
        curl_easy_cleanup(curl);
        return NULL;
    }

    // Check HTTP response code
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code >= 400) {
        log_error("HTTP: Server returned error %ld for %s", response_code, url);
        mem_free(response.data);  // tracked free: matches mem_realloc in write_response_callback
        curl_easy_cleanup(curl);
        return NULL;
    }

    log_debug("HTTP: Successfully downloaded %zu bytes from %s (HTTP %ld)\n", response.size, url, response_code);

    if (content_size) {
        *content_size = response.size;
    }

    curl_easy_cleanup(curl);
    return response.data;
}

// Download HTTP/HTTPS resource to cache, return local file path or memory buffer
char* download_to_cache(const char* url, const char* cache_dir, char** out_cache_path) {
    if (!url || !cache_dir) {
        return NULL;
    }

    // Ensure cache directory exists
    if (!create_dir(cache_dir)) {
        return NULL;
    }

    // Generate cache filename
    char* cache_filename = generate_cache_filename(url, cache_dir);
    if (!cache_filename) {
        return NULL;
    }

    // Check if file already exists in cache
    if (file_exists(cache_filename)) {
        log_debug("HTTP: Using cached file %s", cache_filename);

        // Read cached file
        size_t cached_size = 0;
        char* content = read_binary_file(cache_filename, &cached_size);
        if (content) {
            if (out_cache_path) {
                *out_cache_path = cache_filename;
            } else {
                mem_free(cache_filename); // from file_cache_path() - mem_alloc
            }
            return content;
        }
    }

    // Download content
    size_t content_size;
    char* content = download_http_content(url, &content_size, NULL);
    if (!content) {
        mem_free(cache_filename); // from file_cache_path() - mem_alloc
        return NULL;
    }

    // Save to cache
    if (write_binary_file(cache_filename, content, content_size) == 0) {
        log_debug("HTTP: Cached content to %s", cache_filename);
    } else {
        log_error("HTTP: Failed to write cache file %s", cache_filename);
    }

    if (out_cache_path) {
        *out_cache_path = cache_filename;
    } else {
        mem_free(cache_filename); // from file_cache_path() - mem_alloc
    }

    return content;
}

// Returns an Input* for HTTP/HTTPS URL, using memory and file cache
Input* input_from_http(const char* url, const char* type, const char* flavor, const char* cache_dir) {
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
        type_str = (String*)mem_alloc(sizeof(String) + strlen(type) + 1, MEM_CAT_INPUT_OTHER);
        if (type_str) {
            type_str->len = strlen(type);
            str_copy(type_str->chars, type_str->len + 1, type, type_str->len);
        }
    }

    if (flavor) {
        flavor_str = (String*)mem_alloc(sizeof(String) + strlen(flavor) + 1, MEM_CAT_INPUT_OTHER);
        if (flavor_str) {
            flavor_str->len = strlen(flavor);
            str_copy(flavor_str->chars, flavor_str->len + 1, flavor, flavor_str->len);
        }
    }

    // Parse content using existing input system
    Input* input = input_from_source(content, abs_url, type_str, flavor_str);

    // Cleanup
    mem_free(content); // from lib - uses mem_alloc
    mem_free(cache_path); // from lib - uses mem_alloc
    mem_free(type_str);
    mem_free(flavor_str);
    return input;
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
        char* value_start = buffer + 13;
        while (*value_start == ' ' || *value_start == '\t') value_start++;

        size_t value_len = header_size - (value_start - buffer);
        // Remove trailing CRLF
        while (value_len > 0 && (value_start[value_len-1] == '\r' || value_start[value_len-1] == '\n')) {
            value_len--;
        }

        if (response->content_type) mem_free(response->content_type);
        response->content_type = (char*)mem_alloc(value_len + 1, MEM_CAT_INPUT_OTHER);
        if (response->content_type) {
            memcpy(response->content_type, value_start, value_len);
            response->content_type[value_len] = '\0';
        }
    }

    // Store all headers
    response->response_headers = (char**)mem_realloc(response->response_headers,
                                               (response->response_header_count + 1) * sizeof(char*), MEM_CAT_INPUT_OTHER);
    if (response->response_headers) {
        char* header_copy = (char*)mem_alloc(header_size + 1, MEM_CAT_INPUT_OTHER);
        if (header_copy) {
            memcpy(header_copy, buffer, header_size);
            header_copy[header_size] = '\0';

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
            if (config->body) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, config->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, config->body_size);
            }
        } else if (str_ieq_const(config->method, method_len, "PUT")) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (config->body) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, config->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, config->body_size);
            }
        } else if (str_ieq_const(config->method, method_len, "DELETE")) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (str_ieq_const(config->method, method_len, "PATCH")) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            if (config->body) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, config->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, config->body_size);
            }
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

    log_debug("HTTP: Successfully fetched %zu bytes from %s (HTTP %ld)\n",
           response->size, url, response->status_code);

    // Cleanup
    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);

    return response;
}
