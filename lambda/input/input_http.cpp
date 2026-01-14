// input_http.cpp
// HTTP/HTTPS handling for Lambda Script input system using libcurl
// Handles downloading files to cache and returning Input*

#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include "input.hpp"
#include "../../lib/file.h"
#include "../../lib/log.h"
#include "lib/log.h"

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
    char* new_data = (char*)realloc(response->data, response->size + total_size + 1);

    if (!new_data) {
        fprintf(stderr, "HTTP: Memory allocation failed\n");
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
            fprintf(stderr, "HTTP: Failed to initialize libcurl\n");
            return false;
        }
        curl_initialized = true;
    }
    return true;
}

// Map HTTP Content-Type to file extension for routing
// Returns a static string (no need to free)
const char* content_type_to_extension(const char* content_type) {
    if (!content_type) return nullptr;

    // Extract the main type, ignoring charset and other params
    // e.g., "text/html; charset=utf-8" -> "text/html"
    char main_type[128];
    const char* semicolon = strchr(content_type, ';');
    size_t len = semicolon ? (size_t)(semicolon - content_type) : strlen(content_type);
    if (len >= sizeof(main_type)) len = sizeof(main_type) - 1;
    strncpy(main_type, content_type, len);
    main_type[len] = '\0';

    // Trim trailing whitespace
    while (len > 0 && (main_type[len-1] == ' ' || main_type[len-1] == '\t')) {
        main_type[--len] = '\0';
    }

    // Map common MIME types to extensions
    if (strcasecmp(main_type, "text/html") == 0) return ".html";
    if (strcasecmp(main_type, "application/xhtml+xml") == 0) return ".html";
    if (strcasecmp(main_type, "text/plain") == 0) return ".txt";
    if (strcasecmp(main_type, "text/css") == 0) return ".css";
    if (strcasecmp(main_type, "text/javascript") == 0) return ".js";
    if (strcasecmp(main_type, "application/javascript") == 0) return ".js";
    if (strcasecmp(main_type, "application/json") == 0) return ".json";
    if (strcasecmp(main_type, "text/xml") == 0) return ".xml";
    if (strcasecmp(main_type, "application/xml") == 0) return ".xml";
    if (strcasecmp(main_type, "text/markdown") == 0) return ".md";
    if (strcasecmp(main_type, "text/x-markdown") == 0) return ".md";
    if (strcasecmp(main_type, "application/pdf") == 0) return ".pdf";
    if (strcasecmp(main_type, "image/svg+xml") == 0) return ".svg";
    if (strcasecmp(main_type, "image/png") == 0) return ".png";
    if (strcasecmp(main_type, "image/jpeg") == 0) return ".jpg";
    if (strcasecmp(main_type, "image/gif") == 0) return ".gif";
    if (strcasecmp(main_type, "image/webp") == 0) return ".webp";
    if (strcasecmp(main_type, "application/x-latex") == 0) return ".tex";
    if (strcasecmp(main_type, "text/x-tex") == 0) return ".tex";
    if (strcasecmp(main_type, "application/x-yaml") == 0) return ".yaml";
    if (strcasecmp(main_type, "text/yaml") == 0) return ".yaml";
    if (strcasecmp(main_type, "application/toml") == 0) return ".toml";
    if (strcasecmp(main_type, "text/csv") == 0) return ".csv";

    log_debug("HTTP: Unknown content-type '%s', defaulting to .html", content_type);
    return ".html";  // Default to HTML for unknown types
}

// Generate cache filename from URL (simple hash-based approach)
static char* generate_cache_filename(const char* url, const char* cache_dir) {
    // Simple hash function for URL
    unsigned long hash = 5381;
    const char* str = url;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }

    // Create filename with hash
    char* filename = (char*)malloc(strlen(cache_dir) + 32);
    if (!filename) return NULL;

    snprintf(filename, strlen(cache_dir) + 32, "%s/%08lx.cache", cache_dir, hash);
    return filename;
}

// Download HTTP/HTTPS resource and return content in memory
char* download_http_content(const char* url, size_t* content_size, const HttpConfig* config) {
    if (!init_curl()) {
        return NULL;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "HTTP: Failed to initialize curl handle\n");
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

    // Perform the request
    log_debug("HTTP: Downloading %s\n", url);
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "HTTP: Download failed: %s\n", curl_easy_strerror(res));
        free(response.data);
        curl_easy_cleanup(curl);
        return NULL;
    }

    // Check HTTP response code
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code >= 400) {
        fprintf(stderr, "HTTP: Server returned error %ld for %s\n", response_code, url);
        free(response.data);
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
    struct stat st;
    if (stat(cache_filename, &st) == 0) {
        log_debug("HTTP: Using cached file %s\n", cache_filename);

        // Read cached file
        FILE* file = fopen(cache_filename, "rb");
        if (file) {
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            fseek(file, 0, SEEK_SET);

            char* content = (char*)malloc(file_size + 1);
            if (content) {
                size_t bytes_read = fread(content, 1, file_size, file);
                content[bytes_read] = '\0';
                fclose(file);

                if (out_cache_path) {
                    *out_cache_path = cache_filename;
                } else {
                    free(cache_filename);
                }

                return content;
            }
            fclose(file);
        }
    }

    // Download content
    size_t content_size;
    char* content = download_http_content(url, &content_size, NULL);
    if (!content) {
        free(cache_filename);
        return NULL;
    }

    // Save to cache
    FILE* cache_file = fopen(cache_filename, "wb");
    if (cache_file) {
        fwrite(content, 1, content_size, cache_file);
        fclose(cache_file);
        log_debug("HTTP: Cached content to %s\n", cache_filename);
    } else {
        fprintf(stderr, "HTTP: Failed to write cache file %s: %s\n",
                cache_filename, strerror(errno));
    }

    if (out_cache_path) {
        *out_cache_path = cache_filename;
    } else {
        free(cache_filename);
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
        free(content);
        free(cache_path);
        return NULL;
    }

    // Create type and flavor strings
    String* type_str = NULL;
    String* flavor_str = NULL;

    if (type) {
        type_str = (String*)malloc(sizeof(String) + strlen(type) + 1);
        if (type_str) {
            type_str->len = strlen(type);
            type_str->ref_cnt = 0;
            strcpy(type_str->chars, type);
        }
    }

    if (flavor) {
        flavor_str = (String*)malloc(sizeof(String) + strlen(flavor) + 1);
        if (flavor_str) {
            flavor_str->len = strlen(flavor);
            flavor_str->ref_cnt = 0;
            strcpy(flavor_str->chars, flavor);
        }
    }

    // Parse content using existing input system
    Input* input = input_from_source(content, abs_url, type_str, flavor_str);

    // Cleanup
    free(content);
    free(cache_path);
    free(type_str);
    free(flavor_str);
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
    if (strncasecmp(buffer, "content-type:", 13) == 0) {
        char* value_start = buffer + 13;
        while (*value_start == ' ' || *value_start == '\t') value_start++;

        size_t value_len = header_size - (value_start - buffer);
        // Remove trailing CRLF
        while (value_len > 0 && (value_start[value_len-1] == '\r' || value_start[value_len-1] == '\n')) {
            value_len--;
        }

        if (response->content_type) free(response->content_type);
        response->content_type = (char*)malloc(value_len + 1);
        if (response->content_type) {
            memcpy(response->content_type, value_start, value_len);
            response->content_type[value_len] = '\0';
        }
    }

    // Store all headers
    response->response_headers = (char**)realloc(response->response_headers,
                                               (response->response_header_count + 1) * sizeof(char*));
    if (response->response_headers) {
        char* header_copy = (char*)malloc(header_size + 1);
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
        free(response->data);
        response->data = NULL;
    }

    if (response->content_type) {
        free(response->content_type);
        response->content_type = NULL;
    }

    for (int i = 0; i < response->response_header_count; i++) {
        free(response->response_headers[i]);
    }
    if (response->response_headers) {
        free(response->response_headers);
        response->response_headers = NULL;
    }
    response->response_header_count = 0;

    free(response);
}

// Perform HTTP request with full fetch-like functionality
FetchResponse* http_fetch(const char* url, const FetchConfig* config) {
    if (!init_curl()) { return NULL; }

    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("HTTP: Failed to initialize curl handle\n");
        return NULL;
    }

    FetchResponse* response = (FetchResponse*)calloc(1, sizeof(FetchResponse));
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

    // Method configuration
    if (config && config->method) {
        if (strcasecmp(config->method, "POST") == 0) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (config->body) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, config->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, config->body_size);
            }
        } else if (strcasecmp(config->method, "PUT") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (config->body) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, config->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, config->body_size);
            }
        } else if (strcasecmp(config->method, "DELETE") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (strcasecmp(config->method, "PATCH") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            if (config->body) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, config->body);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, config->body_size);
            }
        } else if (strcasecmp(config->method, "HEAD") == 0) {
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
