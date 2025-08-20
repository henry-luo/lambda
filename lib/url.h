#ifndef URL_H
#define URL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

// URL scheme enumeration
typedef enum {
    URL_SCHEME_UNKNOWN = 0,
    URL_SCHEME_HTTP,
    URL_SCHEME_HTTPS,
    URL_SCHEME_FTP,
    URL_SCHEME_FTPS,
    URL_SCHEME_FILE,
    URL_SCHEME_MAILTO,
    URL_SCHEME_DATA,
    URL_SCHEME_JAVASCRIPT,
    URL_SCHEME_WS,
    URL_SCHEME_WSS,
    URL_SCHEME_CUSTOM
} UrlScheme;

// URL structure following WHATWG URL Standard
typedef struct {
    String* href;           // complete URL string
    String* origin;         // scheme + host + port
    String* protocol;       // scheme + ":"
    String* username;       // username component
    String* password;       // password component
    String* host;           // hostname or IP address
    String* hostname;       // hostname without port
    String* port;           // port number as string
    String* pathname;       // path component
    String* search;         // query string including "?"
    String* hash;           // fragment including "#"
    UrlScheme scheme;       // parsed scheme enum
    uint16_t port_number;   // parsed port as number (0 = default)
    bool is_valid;          // parsing success flag
} Url;

// URL parser structure
typedef struct {
    const char* input;      // input string being parsed
    size_t length;          // length of input
    size_t position;        // current parsing position
    bool has_error;         // error flag
    char error_msg[256];    // error message buffer
} UrlParser;

// Error codes
typedef enum {
    URL_OK = 0,
    URL_ERROR_INVALID_INPUT,
    URL_ERROR_INVALID_SCHEME,
    URL_ERROR_INVALID_HOST,
    URL_ERROR_INVALID_PORT,
    URL_ERROR_INVALID_PATH,
    URL_ERROR_MEMORY_ALLOCATION,
    URL_ERROR_BUFFER_OVERFLOW
} UrlError;

// Core API functions
Url* url_create(void);
void url_destroy(Url* url);
Url* url_parse(const char* input);
Url* url_parse_with_base(const char* input, const Url* base);
UrlError url_parse_into(const char* input, Url* url);

// URL manipulation
String* url_serialize(const Url* url);
bool url_is_valid(const Url* url);
bool url_equals(const Url* a, const Url* b);
Url* url_clone(const Url* url);

// Component setters (return URL_OK on success)
UrlError url_set_href(Url* url, const char* href);
UrlError url_set_protocol(Url* url, const char* protocol);
UrlError url_set_username(Url* url, const char* username);
UrlError url_set_password(Url* url, const char* password);
UrlError url_set_host(Url* url, const char* host);
UrlError url_set_hostname(Url* url, const char* hostname);
UrlError url_set_port(Url* url, const char* port);
UrlError url_set_pathname(Url* url, const char* pathname);
UrlError url_set_search(Url* url, const char* search);
UrlError url_set_hash(Url* url, const char* hash);

// Utility functions
UrlScheme url_scheme_from_string(const char* scheme);
const char* url_scheme_to_string(UrlScheme scheme);
uint16_t url_default_port_for_scheme(UrlScheme scheme);
bool url_scheme_is_special(UrlScheme scheme);

// Getters - return the string value (not String* wrapper)
const char* url_get_href(const Url* url);
const char* url_get_origin(const Url* url);
const char* url_get_protocol(const Url* url);
const char* url_get_username(const Url* url);
const char* url_get_password(const Url* url);
const char* url_get_host(const Url* url);
const char* url_get_hostname(const Url* url);
const char* url_get_port(const Url* url);
const char* url_get_pathname(const Url* url);
const char* url_get_search(const Url* url);
const char* url_get_hash(const Url* url);
uint16_t url_get_port_number(const Url* url);
UrlScheme url_get_scheme(const Url* url);

// Parser functions
UrlParser* url_parser_create(const char* input);
void url_parser_destroy(UrlParser* parser);
UrlError url_parser_parse(UrlParser* parser, Url* url);

// Main parsing functions (in url_parser.c)
UrlError url_parse_into(const char* input, Url* url);
Url* url_parse(const char* input);
Url* url_parse_with_base(const char* input, const Url* base_url);

// Phase 4: Enhanced relative URL resolution functions
Url* url_resolve_relative(const char* input, const Url* base_url);
UrlError url_resolve_relative_into(const char* input, const Url* base_url, Url* result);

// Path normalization and resolution functions
void url_normalize_path(char* path);
char* url_resolve_path(const char* base_path, const char* relative_path);
void url_normalize_path_segments(char** segments, int* segment_count);
char* url_join_path_segments(char** segments, int segment_count);

// Utility functions for relative URL resolution
bool url_is_absolute_url(const char* input);
bool url_starts_with_scheme(const char* input);
bool url_has_authority_prefix(const char* input);
char* url_extract_path_from_authority_relative(const char* input);

// Special case handlers for relative URLs
UrlError url_handle_query_only_relative(const char* input, const Url* base_url, Url* result);
UrlError url_handle_fragment_only_relative(const char* input, const Url* base_url, Url* result);
UrlError url_handle_path_relative(const char* input, const Url* base_url, Url* result);
UrlError url_handle_authority_relative(const char* input, const Url* base_url, Url* result);

// Helper functions for String management
String* url_create_string(const char* str);
void url_free_string(String* str);
String* url_string_clone(const String* str);

#ifdef __cplusplus
}
#endif

#endif // URL_H

