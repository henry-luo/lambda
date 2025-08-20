// lib/url_compat.c - Compatibility layer for lexbor URL functions
// This provides the legacy API while using our new URL parser internally

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

// Forward declarations for lexbor compatibility
typedef struct lxb_url_s lxb_url_t;

struct lxb_url_s {
    char* href;
    char* protocol;
    char* host;
    char* pathname;
    char* search;
    char* hash;
};

// Create a current working directory URL
lxb_url_t* get_current_dir() {
    char cwd_path[1024];
    if (!getcwd(cwd_path, sizeof(cwd_path))) {
        return NULL;
    }
    
    lxb_url_t* url = malloc(sizeof(lxb_url_t));
    if (!url) return NULL;
    
    // Create file:// URL for current directory
    size_t total_len = strlen("file://") + strlen(cwd_path) + 2;
    url->href = malloc(total_len);
    if (!url->href) {
        free(url);
        return NULL;
    }
    
    snprintf(url->href, total_len, "file://%s/", cwd_path);
    url->protocol = strdup("file:");
    url->host = strdup("");
    url->pathname = strdup(cwd_path);
    url->search = strdup("");
    url->hash = strdup("");
    
    return url;
}

// Parse a URL (simplified implementation)
lxb_url_t* parse_url(lxb_url_t* base, const char* url_string) {
    if (!url_string) return NULL;
    
    lxb_url_t* url = malloc(sizeof(lxb_url_t));
    if (!url) return NULL;
    
    // Simple implementation - if it's an absolute path, convert to file://
    if (url_string[0] == '/') {
        size_t total_len = strlen("file://") + strlen(url_string) + 1;
        url->href = malloc(total_len);
        if (!url->href) {
            free(url);
            return NULL;
        }
        snprintf(url->href, total_len, "file://%s", url_string);
    } else if (strncmp(url_string, "http://", 7) == 0 || 
               strncmp(url_string, "https://", 8) == 0 ||
               strncmp(url_string, "file://", 7) == 0) {
        // Already absolute URL
        url->href = strdup(url_string);
    } else {
        // Relative URL - combine with base
        if (base && base->href) {
            size_t base_len = strlen(base->href);
            size_t url_len = strlen(url_string);
            size_t total_len = base_len + url_len + 2;
            
            url->href = malloc(total_len);
            if (!url->href) {
                free(url);
                return NULL;
            }
            
            // Simple concatenation (could be improved)
            if (base->href[base_len - 1] == '/') {
                snprintf(url->href, total_len, "%s%s", base->href, url_string);
            } else {
                snprintf(url->href, total_len, "%s/%s", base->href, url_string);
            }
        } else {
            // No base, treat as relative to current directory
            char cwd_path[1024];
            if (getcwd(cwd_path, sizeof(cwd_path))) {
                size_t total_len = strlen("file://") + strlen(cwd_path) + strlen(url_string) + 3;
                url->href = malloc(total_len);
                if (!url->href) {
                    free(url);
                    return NULL;
                }
                snprintf(url->href, total_len, "file://%s/%s", cwd_path, url_string);
            } else {
                free(url);
                return NULL;
            }
        }
    }
    
    // Initialize other fields (simplified)
    url->protocol = strdup("file:");
    url->host = strdup("");
    url->pathname = strdup(url_string);
    url->search = strdup("");
    url->hash = strdup("");
    
    return url;
}

// Read text content from a URL (simplified implementation)
char* read_text_doc(lxb_url_t* url) {
    if (!url || !url->href) return NULL;
    
    // Extract path from file:// URL
    const char* path = url->href;
    if (strncmp(path, "file://", 7) == 0) {
        path += 7; // Skip "file://"
    }
    
    // Try to read the file
    FILE* file = fopen(path, "r");
    if (!file) {
        printf("Cannot open file: %s\n", path);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(file);
        return NULL;
    }
    
    // Allocate buffer and read file
    char* content = malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';
    
    fclose(file);
    return content;
}

// Destroy lexbor URL
void lxb_url_destroy(lxb_url_t* url) {
    if (!url) return;
    
    free(url->href);
    free(url->protocol);
    free(url->host);
    free(url->pathname);
    free(url->search);
    free(url->hash);
    free(url);
}
