// lib/url_parser.c - URL parsing logic
// Part of Lambda Script URL parser

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "url.h"
#include "log.h"
#include "str.h"

// Create URL parser
UrlParser* url_parser_create(const char* input) {
    if (!input) return NULL;

    UrlParser* parser = malloc(sizeof(UrlParser));
    if (!parser) return NULL;

    parser->input = input;
    parser->length = strlen(input);
    parser->position = 0;
    parser->has_error = false;
    parser->error_msg[0] = '\0';

    return parser;
}

// Destroy URL parser
void url_parser_destroy(UrlParser* parser) {
    free(parser);
}

// Enhanced URL parsing (Phase 2 - Enhanced component parsing)
UrlError url_parse_into(const char* input, Url* url) {
    if (!input || !url) return URL_ERROR_INVALID_INPUT;

    // Reset URL to clean state
    url->is_valid = false;

    const char* current = input;
    const char* start = input;

    // Phase 1: Parse scheme
    const char* colon = strchr(current, ':');
    if (!colon) {
        return URL_ERROR_INVALID_SCHEME;
    }

    // Extract scheme
    size_t scheme_len = colon - current;
    if (scheme_len == 0) {
        return URL_ERROR_INVALID_SCHEME;
    }

    char scheme_buf[32];
    if (scheme_len >= sizeof(scheme_buf)) {
        return URL_ERROR_INVALID_SCHEME;
    }

    strncpy(scheme_buf, current, scheme_len);
    scheme_buf[scheme_len] = '\0';

    // Update scheme
    url->scheme = url_scheme_from_string(scheme_buf);

    // Update protocol field (scheme + ":")
    url_free_string(url->protocol);
    char protocol_buf[64];
    snprintf(protocol_buf, sizeof(protocol_buf), "%s:", scheme_buf);
    url->protocol = url_create_string(protocol_buf);
    if (!url->protocol) {
        return URL_ERROR_MEMORY_ALLOCATION;
    }

    // Move past scheme and colon
    current = colon + 1;

    // Phase 2: Parse authority (//host:port) for hierarchical URLs
    if (current[0] == '/' && current[1] == '/') {
        current += 2; // Skip "//"

        // Find end of authority (next '/', '?', '#', or end of string)
        const char* authority_end = current;
        while (*authority_end && *authority_end != '/' && *authority_end != '?' && *authority_end != '#') {
            authority_end++;
        }

        // Parse authority: [username[:password]@]host[:port]
        const char* at_sign = NULL;
        const char* colon_port = NULL;

        // Look for @ sign (credentials)
        for (const char* p = current; p < authority_end; p++) {
            if (*p == '@') {
                at_sign = p;
                break;
            }
        }

        const char* host_start = at_sign ? (at_sign + 1) : current;
        const char* host_end = authority_end;

        // Look for port colon (from right to left to handle IPv6)
        for (const char* p = authority_end - 1; p >= host_start; p--) {
            if (*p == ':' && (p == host_start || *(p-1) != ':')) {
                colon_port = p;
                host_end = p;
                break;
            }
        }

        // Extract credentials if present
        if (at_sign) {
            const char* colon_cred = NULL;
            for (const char* p = current; p < at_sign; p++) {
                if (*p == ':') {
                    colon_cred = p;
                    break;
                }
            }

            // Extract username
            size_t username_len = colon_cred ? (colon_cred - current) : (at_sign - current);
            if (username_len > 0) {
                char* username_buf = malloc(username_len + 1);
                if (username_buf) {
                    strncpy(username_buf, current, username_len);
                    username_buf[username_len] = '\0';
                    url_free_string(url->username);
                    url->username = url_create_string(username_buf);
                    free(username_buf);
                }
            }

            // Extract password
            if (colon_cred) {
                size_t password_len = at_sign - (colon_cred + 1);
                if (password_len > 0) {
                    char* password_buf = malloc(password_len + 1);
                    if (password_buf) {
                        strncpy(password_buf, colon_cred + 1, password_len);
                        password_buf[password_len] = '\0';
                        url_free_string(url->password);
                        url->password = url_create_string(password_buf);
                        free(password_buf);
                    }
                }
            }
        }

        // Extract host
        size_t host_len = host_end - host_start;
        if (host_len > 0) {
            char host_buf[256];
            if (host_len < sizeof(host_buf)) {
                strncpy(host_buf, host_start, host_len);
                host_buf[host_len] = '\0';
                url_free_string(url->host);
                url_free_string(url->hostname);
                url->host = url_create_string(host_buf);
                url->hostname = url_create_string(host_buf); // Same as host for now
            }
        }

        // Extract port
        if (colon_port) {
            size_t port_len = authority_end - (colon_port + 1);
            if (port_len > 0 && port_len < 16) {
                char port_buf[16];
                strncpy(port_buf, colon_port + 1, port_len);
                port_buf[port_len] = '\0';
                url_free_string(url->port);
                url->port = url_create_string(port_buf);

                // Convert to number
                char* endptr;
                long port_num = strtol(port_buf, &endptr, 10);
                if (*endptr == '\0' && port_num >= 0 && port_num <= 65535) {
                    url->port_number = (uint16_t)port_num;
                }
            }
        } else {
            // Use default port for scheme
            url->port_number = url_default_port_for_scheme(url->scheme);
        }

        current = authority_end;
    }

    // Phase 3: Parse path
    const char* path_start = current;
    const char* path_end = current;

    // Find end of path (next '?' or '#' or end of string)
    while (*path_end && *path_end != '?' && *path_end != '#') {
        path_end++;
    }

    // Extract path
    size_t path_len = path_end - path_start;
    if (path_len > 0) {
        // Use dynamic allocation for paths to handle long URLs
        char* path_buf = malloc(path_len + 1);
        if (path_buf) {
            strncpy(path_buf, path_start, path_len);
            path_buf[path_len] = '\0';
            url_free_string(url->pathname);
            url->pathname = url_create_string(path_buf);
            free(path_buf);
        }
    } else if (url->scheme == URL_SCHEME_HTTP || url->scheme == URL_SCHEME_HTTPS) {
        // Default path for HTTP/HTTPS
        url_free_string(url->pathname);
        url->pathname = url_create_string("/");
    }

    current = path_end;

    // Phase 4: Parse query
    if (*current == '?') {
        current++; // Skip '?'
        const char* query_start = current;
        const char* query_end = current;

        // Find end of query (next '#' or end of string)
        while (*query_end && *query_end != '#') {
            query_end++;
        }

        // Extract query (including the '?')
        size_t query_len = query_end - query_start;
        if (query_len > 0) {
            char* query_buf = malloc(query_len + 2); // +1 for '?', +1 for '\0'
            if (query_buf) {
                query_buf[0] = '?';
                strncpy(query_buf + 1, query_start, query_len);
                query_buf[query_len + 1] = '\0';
                url_free_string(url->search);
                url->search = url_create_string(query_buf);
                free(query_buf);
            }
        }

        current = query_end;
    }

    // Phase 5: Parse fragment
    if (*current == '#') {
        current++; // Skip '#'
        const char* fragment_start = current;

        // Fragment goes to end of string
        size_t fragment_len = strlen(fragment_start);
        if (fragment_len > 0 && fragment_len < 4096) {  // Reasonable limit for URL fragments
            char* fragment_buf = malloc(fragment_len + 2); // +1 for '#', +1 for '\0'
            if (fragment_buf) {
                fragment_buf[0] = '#';
                memcpy(fragment_buf + 1, fragment_start, fragment_len);  // Safe copy
                fragment_buf[fragment_len + 1] = '\0';  // Explicit null termination
                url_free_string(url->hash);
                url->hash = url_create_string(fragment_buf);
                free(fragment_buf);
            }
        }
    }

    // Update href (full input)
    url_free_string(url->href);
    url->href = url_create_string(input);
    if (!url->href) {
        return URL_ERROR_MEMORY_ALLOCATION;
    }

    // Mark as valid if we got this far
    url->is_valid = true;

    return URL_OK;
}

// Parser wrapper
UrlError url_parser_parse(UrlParser* parser, Url* url) {
    if (!parser || !url) return URL_ERROR_INVALID_INPUT;
    return url_parse_into(parser->input, url);
}

// Parse URL from string
Url* url_parse(const char* input) {
    if (!input) return NULL;

    Url* url = url_create();
    if (!url) return NULL;

    if (url_parse_into(input, url) != URL_OK) {
        url_destroy(url);
        return NULL;
    }

    return url;
}

// Href setter that re-parses the URL
UrlError url_set_href(Url* url, const char* href) {
    if (!url || !href) return URL_ERROR_INVALID_INPUT;
    return url_parse_into(href, url);
}

// Helper function to normalize path (remove . and .. segments)
void url_normalize_path(char* path) {
    if (!path || !*path) return;

    char segments[64][256]; // Max 64 path segments, 256 chars each
    int segment_count = 0;

    // Make a copy to work with since strtok modifies the string
    char path_copy[2048];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // Split path into segments
    char* token = strtok(path_copy, "/");

    while (token && segment_count < 63) {
        if (strcmp(token, ".") == 0) {
            // Skip current directory references
        } else if (strcmp(token, "..") == 0) {
            // Go up one directory (remove last segment)
            if (segment_count > 0) {
                segment_count--;
            }
        } else if (strlen(token) > 0) {
            // Regular segment - copy the string
            size_t token_len = strlen(token);
            if (token_len < 256) {
                strncpy(segments[segment_count], token, 255);
                segments[segment_count][255] = '\0';
                segment_count++;
            }
        }

        // Always advance to next token
        token = strtok(NULL, "/");
    }

    // Rebuild path safely
    path[0] = '\0';
    if (segment_count == 0) {
        strncpy(path, "/", 2047);
        return;
    }

    size_t current_len = 0;
    for (int i = 0; i < segment_count && current_len < 2046; i++) {
        // Add slash
        if (current_len < 2047) {
            path[current_len++] = '/';
            path[current_len] = '\0';
        }

        // Add segment
        size_t segment_len = strlen(segments[i]);
        if (current_len + segment_len < 2047) {
            strncpy(path + current_len, segments[i], segment_len);
            current_len += segment_len;
            path[current_len] = '\0';
        }
    }
}

// Enhanced helper function to resolve relative path against base path
char* url_resolve_path(const char* base_path, const char* relative_path) {
    if (!base_path || !relative_path) return NULL;

    char* result = malloc(2048);
    if (!result) return NULL;

    if (relative_path[0] == '/') {
        // Absolute path - use as-is, but normalize it
        strncpy(result, relative_path, 2047);
        result[2047] = '\0';
        url_normalize_path(result);
        return result;
    }

    // RFC 3986 Section 5.2.3 - Relative path resolution
    // The base path segments are parsed, excluding the last segment (filename)
    char* segments[128];
    int segment_count = 0;

    // Parse base path segments, excluding the last one only if it's not a directory
    if (base_path && strlen(base_path) > 1) {
        char base_copy[2048];
        size_t base_len = strlen(base_path + 1);
        if (base_len >= sizeof(base_copy)) {
            log_error("URL path too long: %zu chars (max: %zu)", base_len, sizeof(base_copy) - 1);
            return NULL;  // Graceful failure for oversized paths
        }
        strncpy(base_copy, base_path + 1, sizeof(base_copy) - 1); // Safe copy
        base_copy[sizeof(base_copy) - 1] = '\0';  // Ensure null termination

        // Check if base path ends with '/' (indicating it's a directory)
        bool is_directory = (base_path[strlen(base_path) - 1] == '/');

        // Split into segments
        char* temp_segments[128];
        int temp_count = 0;
        char* token = strtok(base_copy, "/");
        while (token && temp_count < 127) {
            temp_segments[temp_count] = malloc(strlen(token) + 1);
            if (temp_segments[temp_count]) {
                size_t token_len = strlen(token);
                str_copy(temp_segments[temp_count], token_len + 1, token, token_len);
                temp_count++;
            }
            token = strtok(NULL, "/");
        }

        // Copy segments: if directory, keep all; if file, exclude last (RFC 3986)
        int segments_to_copy = is_directory ? temp_count : (temp_count > 0 ? temp_count - 1 : 0);
        for (int i = 0; i < segments_to_copy && i < 127; i++) {
            segments[segment_count] = temp_segments[i];
            segment_count++;
        }

        // Free unused segments
        for (int i = segments_to_copy; i < temp_count; i++) {
            free(temp_segments[i]);
        }
    }

    // Process relative path segments according to RFC 3986 Section 5.2.4
    char path_copy[1024];
    strncpy(path_copy, relative_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char* token = strtok(path_copy, "/");
    while (token && segment_count < 127) {
        if (strcmp(token, ".") == 0) {
            // Current directory - skip (RFC 3986)
        } else if (strcmp(token, "..") == 0) {
            // Parent directory - remove last segment if possible (RFC 3986)
            if (segment_count > 0) {
                free(segments[segment_count - 1]);
                segment_count--;
            }
            // If segment_count == 0, ".." has no effect (can't go above root)
        } else if (strlen(token) > 0) {
            // Regular segment - add it
            segments[segment_count] = malloc(strlen(token) + 1);
            if (segments[segment_count]) {
                size_t token_len = strlen(token);
                str_copy(segments[segment_count], token_len + 1, token, token_len);
                segment_count++;
            }
        }
        token = strtok(NULL, "/");
    }

    // Rebuild path from segments
    str_copy(result, 2048, "/", 1);
    size_t result_len = 1;
    for (int i = 0; i < segment_count; i++) {
        if (segments[i]) {
            if (result_len > 1) {
                result_len = str_cat(result, result_len, 2048, "/", 1);
            }
            result_len = str_cat(result, result_len, 2048, segments[i], strlen(segments[i]));
            free(segments[i]);
        }
    }

    return result;
}

// Enhanced base URL parsing using Phase 4 implementation
Url* url_parse_with_base(const char* input, const Url* base) {
    if (!input) return NULL;

    // If no base URL, just do regular parsing
    if (!base) {
        return url_parse(input);
    }

    // Use the enhanced relative URL resolution
    return url_resolve_relative(input, base);
}

// =============================================================================
// PHASE 4: ENHANCED RELATIVE URL RESOLUTION
// =============================================================================

// Check if a URL string is absolute (has scheme)
bool url_is_absolute_url(const char* input) {
    if (!input || !*input) return false;

    // Look for scheme (must start with letter, then letters/digits/+/-/.)
    const char* p = input;
    if (!isalpha(*p)) return false;

    p++;
    while (*p && (isalnum(*p) || *p == '+' || *p == '-' || *p == '.')) {
        p++;
    }

    return (*p == ':');
}

// Check if input starts with a valid scheme
bool url_starts_with_scheme(const char* input) {
    return url_is_absolute_url(input);
}

// Check if input starts with "//" (authority-relative)
bool url_has_authority_prefix(const char* input) {
    return input && input[0] == '/' && input[1] == '/';
}

// Extract path component from authority-relative URL
char* url_extract_path_from_authority_relative(const char* input) {
    if (!input || !url_has_authority_prefix(input)) return NULL;

    // Skip "//"
    const char* start = input + 2;

    // Find the first '/' after the authority
    const char* path_start = strchr(start, '/');
    if (!path_start) {
        // No path component, return "/"
        char* result = malloc(2);
        if (result) {
            str_copy(result, 2, "/", 1);
        }
        return result;
    }

    // Find end of path (before query or fragment)
    const char* path_end = path_start;
    while (*path_end && *path_end != '?' && *path_end != '#') {
        path_end++;
    }

    size_t path_len = path_end - path_start;
    if (path_len == 0) {
        char* result = malloc(2);
        if (result) {
            str_copy(result, 2, "/", 1);
        }
        return result;
    }

    char* result = malloc(path_len + 1);
    if (result) {
        strncpy(result, path_start, path_len);
        result[path_len] = '\0';
    }

    return result;
}

// Handle query-only relative URLs (e.g., "?query")
UrlError url_handle_query_only_relative(const char* input, const Url* base_url, Url* result) {
    if (!input || !base_url || !result || input[0] != '?') {
        return URL_ERROR_INVALID_INPUT;
    }

    // Copy everything from base except query and fragment
    result->scheme = base_url->scheme;
    result->port_number = base_url->port_number;

    // Clone strings
    url_free_string(result->protocol);
    result->protocol = url_string_clone(base_url->protocol);

    url_free_string(result->username);
    result->username = url_string_clone(base_url->username);

    url_free_string(result->password);
    result->password = url_string_clone(base_url->password);

    url_free_string(result->host);
    result->host = url_string_clone(base_url->host);

    url_free_string(result->hostname);
    result->hostname = url_string_clone(base_url->hostname);

    url_free_string(result->port);
    result->port = url_string_clone(base_url->port);

    url_free_string(result->pathname);
    result->pathname = url_string_clone(base_url->pathname);

    // Parse new query
    const char* query_end = strchr(input, '#');
    size_t query_len = query_end ? (query_end - input) : strlen(input);

    if (query_len > 0 && query_len < 1024) {
        char query_buf[1025];
        strncpy(query_buf, input, query_len);
        query_buf[query_len] = '\0';

        url_free_string(result->search);
        result->search = url_create_string(query_buf);
    }

    // Parse fragment if present
    if (query_end && *(query_end + 1)) {
        char fragment_buf[1025];
        fragment_buf[0] = '#';
        str_copy(fragment_buf + 1, sizeof(fragment_buf) - 1, query_end + 1, strlen(query_end + 1));

        url_free_string(result->hash);
        result->hash = url_create_string(fragment_buf);
    } else {
        url_free_string(result->hash);
        result->hash = NULL;
    }

    return URL_OK;
}

// Handle fragment-only relative URLs (e.g., "#fragment")
UrlError url_handle_fragment_only_relative(const char* input, const Url* base_url, Url* result) {
    if (!input || !base_url || !result || input[0] != '#') {
        return URL_ERROR_INVALID_INPUT;
    }

    // Copy everything from base except fragment
    result->scheme = base_url->scheme;
    result->port_number = base_url->port_number;

    // Clone all string components except hash
    url_free_string(result->protocol);
    result->protocol = url_string_clone(base_url->protocol);

    url_free_string(result->username);
    result->username = url_string_clone(base_url->username);

    url_free_string(result->password);
    result->password = url_string_clone(base_url->password);

    url_free_string(result->host);
    result->host = url_string_clone(base_url->host);

    url_free_string(result->hostname);
    result->hostname = url_string_clone(base_url->hostname);

    url_free_string(result->port);
    result->port = url_string_clone(base_url->port);

    url_free_string(result->pathname);
    result->pathname = url_string_clone(base_url->pathname);

    url_free_string(result->search);
    result->search = url_string_clone(base_url->search);

    // Set new fragment
    if (strlen(input) > 1) {
        url_free_string(result->hash);
        result->hash = url_create_string(input);
    } else {
        url_free_string(result->hash);
        result->hash = NULL;
    }

    return URL_OK;
}

// Handle authority-relative URLs (e.g., "//example.com/path")
UrlError url_handle_authority_relative(const char* input, const Url* base_url, Url* result) {
    if (!input || !base_url || !result || !url_has_authority_prefix(input)) {
        return URL_ERROR_INVALID_INPUT;
    }

    // Copy scheme from base
    result->scheme = base_url->scheme;

    url_free_string(result->protocol);
    result->protocol = url_string_clone(base_url->protocol);

    // Parse the authority-relative URL as if it were absolute
    char temp_url[2048];
    snprintf(temp_url, sizeof(temp_url), "%s:%s",
             url_scheme_to_string(base_url->scheme), input);

    Url* temp = url_parse(temp_url);
    if (!temp || !temp->is_valid) {
        if (temp) url_destroy(temp);
        return URL_ERROR_INVALID_INPUT;
    }

    // Copy parsed components (except scheme which we already set)
    url_free_string(result->username);
    result->username = url_string_clone(temp->username);

    url_free_string(result->password);
    result->password = url_string_clone(temp->password);

    url_free_string(result->host);
    result->host = url_string_clone(temp->host);

    url_free_string(result->hostname);
    result->hostname = url_string_clone(temp->hostname);

    url_free_string(result->port);
    result->port = url_string_clone(temp->port);
    result->port_number = temp->port_number;

    url_free_string(result->pathname);
    result->pathname = url_string_clone(temp->pathname);

    url_free_string(result->search);
    result->search = url_string_clone(temp->search);

    url_free_string(result->hash);
    result->hash = url_string_clone(temp->hash);

    url_destroy(temp);
    return URL_OK;
}

// Enhanced path normalization with proper segment handling
void url_normalize_path_segments(char** segments, int* segment_count) {
    if (!segments || !segment_count || *segment_count <= 0) return;

    int write_index = 0;

    for (int read_index = 0; read_index < *segment_count; read_index++) {
        const char* segment = segments[read_index];

        if (!segment || strlen(segment) == 0) {
            // Skip empty segments
            continue;
        }

        if (strcmp(segment, ".") == 0) {
            // Skip "." segments
            continue;
        }

        if (strcmp(segment, "..") == 0) {
            // ".." segment - remove previous segment if possible
            if (write_index > 0) {
                write_index--;
            }
            continue;
        }

        // Regular segment - keep it
        if (write_index != read_index) {
            segments[write_index] = segments[read_index];
        }
        write_index++;
    }

    *segment_count = write_index;
}

// Join normalized path segments back into a path string
char* url_join_path_segments(char** segments, int segment_count) {
    if (!segments || segment_count < 0) {
        char* result = malloc(2);
        if (result) str_copy(result, 2, "/", 1);
        return result;
    }

    if (segment_count == 0) {
        char* result = malloc(2);
        if (result) str_copy(result, 2, "/", 1);
        return result;
    }

    // Calculate total length needed
    size_t total_len = 1; // Start with 1 for leading slash
    for (int i = 0; i < segment_count; i++) {
        if (segments[i]) {
            total_len += strlen(segments[i]) + 1; // +1 for slash
        }
    }

    char* result = malloc(total_len + 1);
    if (!result) return NULL;

    str_copy(result, total_len + 1, "/", 1);
    size_t result_len = 1;
    for (int i = 0; i < segment_count; i++) {
        if (segments[i] && strlen(segments[i]) > 0) {
            if (result_len > 1) {
                result_len = str_cat(result, result_len, total_len + 1, "/", 1);
            }
            result_len = str_cat(result, result_len, total_len + 1, segments[i], strlen(segments[i]));
        }
    }

    return result;
}

// Handle path-relative URLs (e.g., "path", "../path", "./path")
UrlError url_handle_path_relative(const char* input, const Url* base_url, Url* result) {
    if (!input || !base_url || !result) {
        return URL_ERROR_INVALID_INPUT;
    }

    // Copy everything from base except path, query, and fragment
    result->scheme = base_url->scheme;
    result->port_number = base_url->port_number;

    url_free_string(result->protocol);
    result->protocol = url_string_clone(base_url->protocol);

    url_free_string(result->username);
    result->username = url_string_clone(base_url->username);

    url_free_string(result->password);
    result->password = url_string_clone(base_url->password);

    url_free_string(result->host);
    result->host = url_string_clone(base_url->host);

    url_free_string(result->hostname);
    result->hostname = url_string_clone(base_url->hostname);

    url_free_string(result->port);
    result->port = url_string_clone(base_url->port);

    // Parse input to separate path, query, and fragment
    const char* query_start = strchr(input, '?');
    const char* fragment_start = strchr(input, '#');

    // Determine path end
    const char* path_end = input + strlen(input);
    if (query_start && (!fragment_start || query_start < fragment_start)) {
        path_end = query_start;
    } else if (fragment_start) {
        path_end = fragment_start;
    }

    // Extract relative path
    size_t path_len = path_end - input;
    char relative_path[1024];
    if (path_len > 0 && path_len < sizeof(relative_path) - 1) {
        strncpy(relative_path, input, path_len);
        relative_path[path_len] = '\0';
    } else {
        relative_path[0] = '\0';
    }

    // Resolve path
    const char* base_path = (base_url->pathname && base_url->pathname->len > 0)
                          ? base_url->pathname->chars : "/";

    char* resolved_path = url_resolve_path(base_path, relative_path);
    if (resolved_path) {
        url_free_string(result->pathname);
        result->pathname = url_create_string(resolved_path);
        free(resolved_path);
    } else {
        url_free_string(result->pathname);
        result->pathname = url_create_string("/");
    }

    // Handle query
    if (query_start) {
        const char* query_end = fragment_start ? fragment_start : (input + strlen(input));
        size_t query_len = query_end - query_start;

        if (query_len > 0 && query_len < 1024) {
            char query_buf[1025];
            strncpy(query_buf, query_start, query_len);
            query_buf[query_len] = '\0';

            url_free_string(result->search);
            result->search = url_create_string(query_buf);
        }
    } else {
        url_free_string(result->search);
        result->search = NULL;
    }

    // Handle fragment
    if (fragment_start) {
        url_free_string(result->hash);
        result->hash = url_create_string(fragment_start);
    } else {
        url_free_string(result->hash);
        result->hash = NULL;
    }

    return URL_OK;
}

// Main enhanced relative URL resolution function
Url* url_resolve_relative(const char* input, const Url* base_url) {
    // Robust parameter validation
    if (!input || !base_url) {
        return NULL;
    }

    // Additional safety checks to catch parameter order mistakes
    // Check if base_url looks like a valid URL structure pointer
    // If someone passes (base_url, "string"), base_url would be treated as input (char*)
    // and "string" would be treated as base_url (Url*), leading to crashes

    // Try to detect if base_url is actually a string pointer by checking
    // if it points to readable ASCII data that looks like a URL
    const char* potential_string = (const char*)base_url;
    if (potential_string != NULL &&
        ((strncmp(potential_string, "http://", 7) == 0) ||
         (strncmp(potential_string, "https://", 8) == 0) ||
         (strncmp(potential_string, "ftp://", 6) == 0) ||
         (strncmp(potential_string, "file://", 7) == 0))) {
        // This suggests the user passed parameters in wrong order
        return NULL;
    }

    // Check for reasonable scheme value to catch corrupted pointers
    if (base_url->scheme < URL_SCHEME_UNKNOWN || base_url->scheme > URL_SCHEME_CUSTOM) {
        return NULL;
    }

    Url* result = url_create();
    if (!result) return NULL;

    UrlError error = url_resolve_relative_into(input, base_url, result);
    if (error != URL_OK) {
        url_destroy(result);
        return NULL;
    }

    return result;
}

// Enhanced relative URL resolution implementation
UrlError url_resolve_relative_into(const char* input, const Url* base_url, Url* result) {
    // Comprehensive parameter validation
    if (!input || !base_url || !result) {
        return URL_ERROR_INVALID_INPUT;
    }

    // Additional safety checks to catch parameter order mistakes
    const char* potential_string = (const char*)base_url;
    if (potential_string != NULL &&
        ((strncmp(potential_string, "http://", 7) == 0) ||
         (strncmp(potential_string, "https://", 8) == 0) ||
         (strncmp(potential_string, "ftp://", 6) == 0) ||
         (strncmp(potential_string, "file://", 7) == 0))) {
        // This suggests the user passed parameters in wrong order
        return URL_ERROR_INVALID_INPUT;
    }

    // Check for reasonable scheme value to catch corrupted pointers
    if (base_url->scheme < URL_SCHEME_UNKNOWN || base_url->scheme > URL_SCHEME_CUSTOM) {
        return URL_ERROR_INVALID_INPUT;
    }

    // Validate that base_url has essential components for relative resolution
    // File URLs can have NULL or empty hostnames, so be more flexible
    if (base_url->scheme != URL_SCHEME_FILE &&
        (!base_url->hostname || !base_url->hostname->chars[0])) {
        return URL_ERROR_INVALID_INPUT;
    }

    // Skip leading and trailing whitespace
    while (*input && isspace(*input)) input++;

    // Trim trailing whitespace
    const char* end = input + strlen(input) - 1;
    while (end > input && isspace(*end)) end--;
    size_t input_len = end - input + 1;

    // Create trimmed input string
    char trimmed_input[2048];
    if (input_len > 0 && input_len < sizeof(trimmed_input)) {
        strncpy(trimmed_input, input, input_len);
        trimmed_input[input_len] = '\0';
        input = trimmed_input;
    }

    if (!*input) {
        // Empty input - return copy of base URL
        if (base_url) {
            // Copy all components from base
            result->scheme = base_url->scheme;
            result->port_number = base_url->port_number;

            url_free_string(result->href);
            result->href = url_string_clone(base_url->href);

            url_free_string(result->protocol);
            result->protocol = url_string_clone(base_url->protocol);

            url_free_string(result->username);
            result->username = url_string_clone(base_url->username);

            url_free_string(result->password);
            result->password = url_string_clone(base_url->password);

            url_free_string(result->host);
            result->host = url_string_clone(base_url->host);

            url_free_string(result->hostname);
            result->hostname = url_string_clone(base_url->hostname);

            url_free_string(result->port);
            result->port = url_string_clone(base_url->port);

            url_free_string(result->pathname);
            result->pathname = url_string_clone(base_url->pathname);

            url_free_string(result->search);
            result->search = url_string_clone(base_url->search);

            url_free_string(result->hash);
            result->hash = url_string_clone(base_url->hash);

            result->is_valid = base_url->is_valid;
            return URL_OK;
        } else {
            return URL_ERROR_INVALID_INPUT;
        }
    }

    // Check if input is absolute URL
    if (url_is_absolute_url(input)) {
        return url_parse_into(input, result);
    }

    // Input is relative - need base URL
    if (!base_url || !base_url->is_valid) {
        return URL_ERROR_INVALID_INPUT;
    }

    UrlError error;

    // Determine type of relative URL and handle accordingly
    if (input[0] == '#') {
        // Fragment-only relative URL
        error = url_handle_fragment_only_relative(input, base_url, result);
    } else if (input[0] == '?') {
        // Query-only relative URL
        error = url_handle_query_only_relative(input, base_url, result);
    } else if (url_has_authority_prefix(input)) {
        // Authority-relative URL (starts with "//")
        error = url_handle_authority_relative(input, base_url, result);
    } else if (input[0] == '/') {
        // Absolute path relative URL
        result->scheme = base_url->scheme;
        result->port_number = base_url->port_number;

        // Copy authority components
        url_free_string(result->protocol);
        result->protocol = url_string_clone(base_url->protocol);

        url_free_string(result->username);
        result->username = url_string_clone(base_url->username);

        url_free_string(result->password);
        result->password = url_string_clone(base_url->password);

        url_free_string(result->host);
        result->host = url_string_clone(base_url->host);

        url_free_string(result->hostname);
        result->hostname = url_string_clone(base_url->hostname);

        url_free_string(result->port);
        result->port = url_string_clone(base_url->port);

        // Parse path, query, and fragment from input
        const char* query_start = strchr(input, '?');
        const char* fragment_start = strchr(input, '#');

        // Extract path
        const char* path_end = input + strlen(input);
        if (query_start && (!fragment_start || query_start < fragment_start)) {
            path_end = query_start;
        } else if (fragment_start) {
            path_end = fragment_start;
        }

        size_t path_len = path_end - input;
        if (path_len > 0 && path_len < 1024) {
            char path_buf[1024];
            strncpy(path_buf, input, path_len);
            path_buf[path_len] = '\0';

            // Normalize the path
            url_normalize_path(path_buf);

            url_free_string(result->pathname);
            result->pathname = url_create_string(path_buf);
        } else {
            url_free_string(result->pathname);
            result->pathname = url_create_string("/");
        }

        // Handle query and fragment
        if (query_start) {
            const char* query_end = fragment_start ? fragment_start : (input + strlen(input));
            size_t query_len = query_end - query_start;

            if (query_len > 0 && query_len < 1024) {
                char query_buf[1025];
                strncpy(query_buf, query_start, query_len);
                query_buf[query_len] = '\0';

                url_free_string(result->search);
                result->search = url_create_string(query_buf);
            }
        } else {
            url_free_string(result->search);
            result->search = NULL;
        }

        if (fragment_start) {
            url_free_string(result->hash);
            result->hash = url_create_string(fragment_start);
        } else {
            url_free_string(result->hash);
            result->hash = NULL;
        }

        error = URL_OK;
    } else {
        // Path-relative URL
        error = url_handle_path_relative(input, base_url, result);
    }

    if (error != URL_OK) {
        return error;
    }

    // Build complete href using the robust function from url.c
    url_free_string(result->href);
    result->href = url_construct_href(result);
    if (!result->href) {
        return URL_ERROR_MEMORY_ALLOCATION;
    }

    result->is_valid = true;
    return URL_OK;
}
