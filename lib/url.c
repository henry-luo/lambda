// lib/url.c - Core URL structure and utilities
// Part of Lambda Script URL parser

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "url.h"

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
Url* url_create(void) {
    Url* url = calloc(1, sizeof(Url));
    if (!url) return NULL;
    
    // Initialize with default values
    url->scheme = URL_SCHEME_UNKNOWN;
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
    if (len >= sizeof(lower)) return URL_SCHEME_CUSTOM;
    
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
    
    return URL_SCHEME_CUSTOM;
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

// URL serialization
String* url_serialize(const Url* url) {
    if (!url || !url->href) return NULL;
    return url_string_clone(url->href);
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