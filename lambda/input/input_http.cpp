// input_http.cpp
// HTTP/HTTPS handling for Lambda Script input system using libcurl
// Handles downloading files to cache and returning Input*

#include "input.h"
#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

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

// Create cache directory if it doesn't exist
static bool ensure_cache_directory(const char* cache_dir) {
    struct stat st = {0};
    
    if (stat(cache_dir, &st) == -1) {
        if (mkdir(cache_dir, 0755) == -1) {
            fprintf(stderr, "HTTP: Failed to create cache directory %s: %s\n", 
                    cache_dir, strerror(errno));
            return false;
        }
    }
    return true;
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
    printf("HTTP: Downloading %s\n", url);
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
    
    printf("HTTP: Successfully downloaded %zu bytes from %s (HTTP %ld)\n", 
           response.size, url, response_code);
    
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
    if (!ensure_cache_directory(cache_dir)) {
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
        printf("HTTP: Using cached file %s\n", cache_filename);
        
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
        printf("HTTP: Cached content to %s\n", cache_filename);
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
