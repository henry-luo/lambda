// lib/url.c - Core URL structure and utilities
// Part of Lambda Script URL parser

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>  // for getcwd and chdir
#include "url.h"
#include "log.h"

// String allocation helper
String* url_create_string(const char* value) {
    if (!value) return NULL;
    
    size_t len = strlen(value);
    // Allocate space for String struct plus the string data
    String* str = malloc(sizeof(String) + len + 1);
    if (!str) return NULL;
    
    str->len = len;
    str->ref_cnt = 1;
    strcpy(str->chars, value);
    return str;
}

// String deallocation helper
void url_free_string(String* str) {
    if (!str) return;
    free(str);
}

// String cloning helper
String* url_string_clone(const String* str) {
    if (!str) return NULL;
    return url_create_string(str->chars);
}

// Create new URL structure
Url* url_create() {
    Url* url = malloc(sizeof(Url));
    if (!url) return NULL;
    
    // Initialize all fields to NULL - they will be set during parsing
    url->scheme = URL_SCHEME_UNKNOWN;
    url->username = NULL;
    url->password = NULL;
    url->hostname = NULL;
    url->port = NULL;
    url->pathname = url_create_string("/");  // Default pathname
    url->search = NULL;
    url->hash = NULL;
    url->href = NULL;
    url->origin = NULL;
    url->protocol = NULL;
    url->host = url_create_string("");  // Default empty host
    url->port_number = 0;
    url->is_valid = false;
    
    return url;
}

// Clone URL structure
Url* url_clone(const Url* src) {
    if (!src) return NULL;
    
    Url* url = url_create();
    if (!url) return NULL;
    
    // Copy primitive fields
    url->scheme = src->scheme;
    url->port_number = src->port_number;
    url->is_valid = src->is_valid;
    
    // Clone string fields
    if (src->href) url->href = url_string_clone(src->href);
    if (src->origin) url->origin = url_string_clone(src->origin);
    if (src->protocol) url->protocol = url_string_clone(src->protocol);
    if (src->username) url->username = url_string_clone(src->username);
    if (src->password) url->password = url_string_clone(src->password);
    if (src->host) url->host = url_string_clone(src->host);
    if (src->hostname) url->hostname = url_string_clone(src->hostname);
    if (src->port) url->port = url_string_clone(src->port);
    if (src->pathname) url->pathname = url_string_clone(src->pathname);
    if (src->search) url->search = url_string_clone(src->search);
    if (src->hash) url->hash = url_string_clone(src->hash);
    
    return url;
}

// Destroy URL structure
void url_destroy(Url* url) {
    if (!url) return;
    
    url_free_string(url->href);
    url_free_string(url->origin);
    url_free_string(url->protocol);
    url_free_string(url->username);
    url_free_string(url->password);
    url_free_string(url->host);
    url_free_string(url->hostname);
    url_free_string(url->port);
    url_free_string(url->pathname);
    url_free_string(url->search);
    url_free_string(url->hash);
    
    free(url);
}

// URL scheme utilities
const char* url_scheme_to_string(UrlScheme scheme) {
    switch (scheme) {
        case URL_SCHEME_HTTP: return "http";
        case URL_SCHEME_HTTPS: return "https";
        case URL_SCHEME_FTP: return "ftp";
        case URL_SCHEME_FTPS: return "ftps";
        case URL_SCHEME_FILE: return "file";
        case URL_SCHEME_MAILTO: return "mailto";
        case URL_SCHEME_DATA: return "data";
        case URL_SCHEME_JAVASCRIPT: return "javascript";
        case URL_SCHEME_WS: return "ws";
        case URL_SCHEME_WSS: return "wss";
        case URL_SCHEME_SYS: return "sys";
        case URL_SCHEME_CUSTOM: return "custom";
        case URL_SCHEME_UNKNOWN:
        default: return "unknown";
    }
}

UrlScheme url_scheme_from_string(const char* scheme) {
    if (!scheme) return URL_SCHEME_UNKNOWN;
    
    // Convert to lowercase for comparison
    char lower[32];
    size_t len = strlen(scheme);
    if (len >= sizeof(lower)) return URL_SCHEME_UNKNOWN;
    
    for (size_t i = 0; i < len; i++) {
        lower[i] = tolower(scheme[i]);
    }
    lower[len] = '\0';
    
    if (strcmp(lower, "http") == 0) return URL_SCHEME_HTTP;
    if (strcmp(lower, "https") == 0) return URL_SCHEME_HTTPS;
    if (strcmp(lower, "ftp") == 0) return URL_SCHEME_FTP;
    if (strcmp(lower, "ftps") == 0) return URL_SCHEME_FTPS;
    if (strcmp(lower, "file") == 0) return URL_SCHEME_FILE;
    if (strcmp(lower, "mailto") == 0) return URL_SCHEME_MAILTO;
    if (strcmp(lower, "data") == 0) return URL_SCHEME_DATA;
    if (strcmp(lower, "javascript") == 0) return URL_SCHEME_JAVASCRIPT;
    if (strcmp(lower, "ws") == 0) return URL_SCHEME_WS;
    if (strcmp(lower, "wss") == 0) return URL_SCHEME_WSS;
    if (strcmp(lower, "sys") == 0) return URL_SCHEME_SYS;
    
    return URL_SCHEME_UNKNOWN;
}

uint16_t url_default_port_for_scheme(UrlScheme scheme) {
    switch (scheme) {
        case URL_SCHEME_HTTP: return 80;
        case URL_SCHEME_HTTPS: return 443;
        case URL_SCHEME_FTP: return 21;
        case URL_SCHEME_FTPS: return 990;
        case URL_SCHEME_WS: return 80;
        case URL_SCHEME_WSS: return 443;
        default: return 0;
    }
}

bool url_scheme_is_special(UrlScheme scheme) {
    switch (scheme) {
        case URL_SCHEME_HTTP:
        case URL_SCHEME_HTTPS:
        case URL_SCHEME_FTP:
        case URL_SCHEME_FILE:
        case URL_SCHEME_WS:
        case URL_SCHEME_WSS:
            return true;
        default:
            return false;
    }
}

// URL validation
bool url_is_valid(const Url* url) {
    return url && url->is_valid;
}

// URL serialization functions - Phase 5 implementation

// Main URL serialization function
String* url_serialize(const Url* url) {
    if (!url) return NULL;
    
    // If we have a complete href, use it for exact roundtrip compatibility
    if (url->href) {
        return url_string_clone(url->href);
    }
    
    // Otherwise, construct the URL from components
    return url_construct_href(url);
}

// Construct complete URL from components
String* url_construct_href(const Url* url) {
    if (!url) return NULL;
    
    // Calculate required buffer size
    size_t total_size = 0;
    
    // Protocol
    if (url->protocol) total_size += url->protocol->len;
    else if (url->scheme != URL_SCHEME_UNKNOWN) {
        total_size += strlen(url_scheme_to_string(url->scheme)) + 1; // +1 for ':'
    }
    
    // Authority section (//)
    bool has_authority = (url->hostname && url->hostname->len > 0) || 
                        (url->host && url->host->len > 0);
    if (has_authority) total_size += 2; // "//"
    
    // Credentials
    if (url->username && url->username->len > 0) {
        total_size += url->username->len;
        if (url->password && url->password->len > 0) {
            total_size += 1 + url->password->len; // ":"
        }
        total_size += 1; // "@"
    }
    
    // Host
    if (url->host && url->host->len > 0) {
        total_size += url->host->len;
    } else if (url->hostname && url->hostname->len > 0) {
        total_size += url->hostname->len;
        // Port
        if (url->port && url->port->len > 0) {
            uint16_t default_port = url_default_port_for_scheme(url->scheme);
            if (url->port_number != default_port || default_port == 0) {
                total_size += 1 + url->port->len; // ":"
            }
        }
    }
    
    // Path
    if (url->pathname) total_size += url->pathname->len;
    else if (has_authority) total_size += 1; // default "/"
    
    // Query
    if (url->search) total_size += url->search->len;
    
    // Fragment
    if (url->hash) total_size += url->hash->len;
    
    // Allocate buffer
    String* result = malloc(sizeof(String) + total_size + 1);
    if (!result) return NULL;
    
    result->len = 0;
    result->ref_cnt = 1;
    char* buffer = result->chars;
    size_t pos = 0;
    
    // Build URL string
    // Protocol
    if (url->protocol) {
        strcpy(buffer + pos, url->protocol->chars);
        pos += url->protocol->len;
    } else if (url->scheme != URL_SCHEME_UNKNOWN) {
        const char* scheme_str = url_scheme_to_string(url->scheme);
        strcpy(buffer + pos, scheme_str);
        pos += strlen(scheme_str);
        buffer[pos++] = ':';
    }
    
    // Authority
    if (has_authority) {
        buffer[pos++] = '/';
        buffer[pos++] = '/';
        
        // Credentials
        if (url->username && url->username->len > 0) {
            strcpy(buffer + pos, url->username->chars);
            pos += url->username->len;
            
            if (url->password && url->password->len > 0) {
                buffer[pos++] = ':';
                strcpy(buffer + pos, url->password->chars);
                pos += url->password->len;
            }
            buffer[pos++] = '@';
        }
        
        // Host
        if (url->host && url->host->len > 0) {
            strcpy(buffer + pos, url->host->chars);
            pos += url->host->len;
        } else if (url->hostname && url->hostname->len > 0) {
            strcpy(buffer + pos, url->hostname->chars);
            pos += url->hostname->len;
            
            // Port (only if not default for scheme)
            if (url->port && url->port->len > 0) {
                uint16_t default_port = url_default_port_for_scheme(url->scheme);
                if (url->port_number != default_port || default_port == 0) {
                    buffer[pos++] = ':';
                    strcpy(buffer + pos, url->port->chars);
                    pos += url->port->len;
                }
            }
        }
    }
    
    // Path
    if (url->pathname && url->pathname->len > 0) {
        strcpy(buffer + pos, url->pathname->chars);
        pos += url->pathname->len;
    } else if (has_authority) {
        buffer[pos++] = '/';
    }
    
    // Query
    if (url->search && url->search->len > 0) {
        strcpy(buffer + pos, url->search->chars);
        pos += url->search->len;
    }
    
    // Fragment
    if (url->hash && url->hash->len > 0) {
        strcpy(buffer + pos, url->hash->chars);
        pos += url->hash->len;
    }
    
    buffer[pos] = '\0';
    result->len = pos;
    
    return result;
}

// Serialize URL without fragment
String* url_serialize_without_fragment(const Url* url) {
    if (!url) return NULL;
    
    // Create a copy of the URL without fragment
    Url temp_url = *url;
    temp_url.hash = NULL;
    
    return url_construct_href(&temp_url);
}

// Serialize origin (scheme + host + port)
String* url_serialize_origin(const Url* url) {
    if (!url) return NULL;
    
    // Calculate required buffer size
    size_t total_size = 0;
    
    // Protocol
    if (url->protocol) total_size += url->protocol->len;
    else if (url->scheme != URL_SCHEME_UNKNOWN) {
        total_size += strlen(url_scheme_to_string(url->scheme)) + 1;
    }
    
    // For origin, prefer hostname + port over combined host
    if (url->hostname && url->hostname->len > 0) {
        total_size += 2 + url->hostname->len; // "//"
        // For origin, always include port if it was explicitly specified
        if (url->port && url->port->len > 0) {
            total_size += 1 + url->port->len; // ":"
        }
    } else if (url->host && url->host->len > 0) {
        total_size += 2 + url->host->len; // "//"
    }
    
    // Allocate buffer
    String* result = malloc(sizeof(String) + total_size + 1);
    if (!result) return NULL;
    
    result->len = 0;
    result->ref_cnt = 1;
    char* buffer = result->chars;
    size_t pos = 0;
    
    // Protocol
    if (url->protocol) {
        strcpy(buffer + pos, url->protocol->chars);
        pos += url->protocol->len;
    } else if (url->scheme != URL_SCHEME_UNKNOWN) {
        const char* scheme_str = url_scheme_to_string(url->scheme);
        strcpy(buffer + pos, scheme_str);
        pos += strlen(scheme_str);
        buffer[pos++] = ':';
    }
    
    // Host - prefer hostname + port for proper origin construction
    if (url->hostname && url->hostname->len > 0) {
        buffer[pos++] = '/';
        buffer[pos++] = '/';
        strcpy(buffer + pos, url->hostname->chars);
        pos += url->hostname->len;
        
        // For origin, include port if it was explicitly specified
        if (url->port && url->port->len > 0) {
            buffer[pos++] = ':';
            strcpy(buffer + pos, url->port->chars);
            pos += url->port->len;
        }
    } else if (url->host && url->host->len > 0) {
        buffer[pos++] = '/';
        buffer[pos++] = '/';
        strcpy(buffer + pos, url->host->chars);
        pos += url->host->len;
    }
    
    buffer[pos] = '\0';
    result->len = pos;
    
    return result;
}

// Component serialization functions
String* url_serialize_scheme(const Url* url) {
    if (!url) return NULL;
    
    if (url->protocol) {
        return url_string_clone(url->protocol);
    } else if (url->scheme != URL_SCHEME_UNKNOWN) {
        const char* scheme_str = url_scheme_to_string(url->scheme);
        size_t len = strlen(scheme_str);
        String* result = malloc(sizeof(String) + len + 2); // +1 for ':' +1 for '\0'
        if (!result) return NULL;
        
        result->len = len + 1;
        result->ref_cnt = 1;
        strcpy(result->chars, scheme_str);
        result->chars[len] = ':';
        result->chars[len + 1] = '\0';
        
        return result;
    }
    
    return NULL;
}

String* url_serialize_host(const Url* url) {
    if (!url) return NULL;
    
    // Prefer hostname + port logic over combined host for proper serialization
    if (url->hostname && url->hostname->len > 0) {
        // Include port if it was explicitly set and is not the default
        if (url->port && url->port->len > 0) {
            uint16_t default_port = url_default_port_for_scheme(url->scheme);
            // Include port if it's not the default for this scheme
            if (url->port_number != default_port) {
                size_t total_len = url->hostname->len + 1 + url->port->len;
                String* result = malloc(sizeof(String) + total_len + 1);
                if (!result) return NULL;
                
                result->len = total_len;
                result->ref_cnt = 1;
                strcpy(result->chars, url->hostname->chars);
                result->chars[url->hostname->len] = ':';
                strcpy(result->chars + url->hostname->len + 1, url->port->chars);
                
                return result;
            }
        }
        
        return url_string_clone(url->hostname);
    } else if (url->host && url->host->len > 0) {
        return url_string_clone(url->host);
    }
    
    return NULL;
}

String* url_serialize_path(const Url* url) {
    if (!url) return NULL;
    
    if (url->pathname) {
        return url_string_clone(url->pathname);
    }
    
    // Default path for URLs with authority is "/"
    bool has_authority = (url->hostname && url->hostname->len > 0) || 
                        (url->host && url->host->len > 0);
    if (has_authority) {
        return url_create_string("/");
    }
    
    return NULL;
}

// URL equality check
bool url_equals(const Url* a, const Url* b) {
    if (!a || !b) return false;
    if (a == b) return true;
    
    if (!a->href || !b->href) return false;
    
    return strcmp(a->href->chars, b->href->chars) == 0;
}

// Getters - return the actual string value, not the String* wrapper
const char* url_get_href(const Url* url) {
    return (url && url->href) ? url->href->chars : NULL;
}

const char* url_get_origin(const Url* url) {
    return (url && url->origin) ? url->origin->chars : NULL;
}

const char* url_get_protocol(const Url* url) {
    return (url && url->protocol) ? url->protocol->chars : NULL;
}

const char* url_get_username(const Url* url) {
    return (url && url->username) ? url->username->chars : NULL;
}

const char* url_get_password(const Url* url) {
    return (url && url->password) ? url->password->chars : NULL;
}

const char* url_get_host(const Url* url) {
    return (url && url->host) ? url->host->chars : NULL;
}

const char* url_get_hostname(const Url* url) {
    return (url && url->hostname) ? url->hostname->chars : NULL;
}

const char* url_get_port(const Url* url) {
    return (url && url->port) ? url->port->chars : NULL;
}

const char* url_get_pathname(const Url* url) {
    return (url && url->pathname) ? url->pathname->chars : NULL;
}

const char* url_get_search(const Url* url) {
    return (url && url->search) ? url->search->chars : NULL;
}

const char* url_get_hash(const Url* url) {
    return (url && url->hash) ? url->hash->chars : NULL;
}

uint16_t url_get_port_number(const Url* url) {
    return url ? url->port_number : 0;
}

UrlScheme url_get_scheme(const Url* url) {
    return url ? url->scheme : URL_SCHEME_UNKNOWN;
}

// Basic component setters
UrlError url_set_protocol(Url* url, const char* protocol) {
    if (!url || !protocol) return URL_ERROR_INVALID_INPUT;
    
    url_free_string(url->protocol);
    url->protocol = url_create_string(protocol);
    return url->protocol ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

UrlError url_set_username(Url* url, const char* username) {
    if (!url || !username) return URL_ERROR_INVALID_INPUT;
    url_free_string(url->username);
    url->username = url_create_string(username);
    return url->username ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

UrlError url_set_password(Url* url, const char* password) {
    if (!url || !password) return URL_ERROR_INVALID_INPUT;
    url_free_string(url->password);
    url->password = url_create_string(password);
    return url->password ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

UrlError url_set_host(Url* url, const char* host) {
    if (!url || !host) return URL_ERROR_INVALID_INPUT;
    url_free_string(url->host);
    url->host = url_create_string(host);
    return url->host ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

UrlError url_set_hostname(Url* url, const char* hostname) {
    if (!url || !hostname) return URL_ERROR_INVALID_INPUT;
    url_free_string(url->hostname);
    url->hostname = url_create_string(hostname);
    return url->hostname ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

UrlError url_set_port(Url* url, const char* port) {
    if (!url || !port) return URL_ERROR_INVALID_INPUT;
    url_free_string(url->port);
    url->port = url_create_string(port);
    return url->port ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

UrlError url_set_pathname(Url* url, const char* pathname) {
    if (!url || !pathname) return URL_ERROR_INVALID_INPUT;
    url_free_string(url->pathname);
    url->pathname = url_create_string(pathname);
    return url->pathname ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

UrlError url_set_search(Url* url, const char* search) {
    if (!url || !search) return URL_ERROR_INVALID_INPUT;
    url_free_string(url->search);
    url->search = url_create_string(search);
    return url->search ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

UrlError url_set_hash(Url* url, const char* hash) {
    if (!url || !hash) return URL_ERROR_INVALID_INPUT;
    url_free_string(url->hash);
    url->hash = url_create_string(hash);
    return url->hash ? URL_OK : URL_ERROR_MEMORY_ALLOCATION;
}

// Helper function to get current working directory as file URL
Url* get_current_dir() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }
    log_debug("Current working directory: %s", cwd);
    
    // Convert to file:// URL format
    char file_url[1200];
    if (cwd[0] == '/') {  // Mac or Linux
        snprintf(file_url, sizeof(file_url), "file://%s", cwd);
    } else { // Windows
        // convert \ to /
        int slen = strlen(cwd);
        for (size_t i = 0; i < slen; i++) {
            if (cwd[i] == '\\') { cwd[i] = '/'; }
        }
        snprintf(file_url, sizeof(file_url), "file:///%s/", cwd);
    }
    return url_parse(file_url);
}

Url* parse_url(Url *base, const char* doc_url) {
    if (!doc_url) return NULL;
    if (base) {
        return url_parse_with_base(doc_url, base);
    } else {
        return url_parse(doc_url);
    }
}