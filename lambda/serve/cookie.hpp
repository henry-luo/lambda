/**
 * @file cookie.hpp
 * @brief Cookie parsing and Set-Cookie header generation
 *
 * Parses Cookie request headers and generates Set-Cookie response headers.
 * Unified from Express res.cookie() and Flask resp.set_cookie() patterns.
 */

#pragma once

#include "serve_types.hpp"
#include "http_response.hpp"

// ============================================================================
// Cookie Parsing (request-side)
// ============================================================================

// parse a Cookie header value into HttpHeader key-value list
// "name1=value1; name2=value2" → linked list of {name, value} pairs
HttpHeader* cookie_parse(const char *cookie_header);

// ============================================================================
// Set-Cookie Generation (response-side)
// ============================================================================

// generate a Set-Cookie header string from name, value, and options
// caller must serve_free() the returned string
char* cookie_build_set_cookie(const char *name, const char *value,
                              const CookieOptions *opts);

// generate a Set-Cookie header that clears (expires) a cookie
char* cookie_build_clear(const char *name, const char *path, const char *domain);
