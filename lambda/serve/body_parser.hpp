/**
 * @file body_parser.hpp
 * @brief Request body parsing — JSON and URL-encoded form data
 *
 * Parses request body based on Content-Type header.
 * Multipart form data is deferred to a future phase.
 *
 * Express equivalents:  express.json(), express.urlencoded()
 * Flask equivalents:    request.json, request.form
 */

#pragma once

#include "http_request.hpp"

// ============================================================================
// Body Parser Results
// ============================================================================

// parsed JSON body — stored as a raw string copy for now.
// in the future, this integrates with Lambda's Item system for structured data.
struct ParsedJson {
    char *json_str;         // validated JSON string (serve_malloc'd)
    size_t len;
};

// parsed form body — key=value pairs as HttpHeader list
// already defined in serve_types.hpp: HttpHeader

// ============================================================================
// Body Parsing Functions
// ============================================================================

// parse request body based on Content-Type.
// sets req->parsed_body and req->parsed_body_type.
// returns 0 on success, -1 on parse error.
int body_parse(HttpRequest *req);

// parse JSON body. returns ParsedJson* (caller frees via body_free_parsed).
ParsedJson* body_parse_json(const char *data, size_t len);

// parse URL-encoded form body. returns HttpHeader* key-value list.
HttpHeader* body_parse_form(const char *data, size_t len);

// free a parsed body based on its type
void body_free_parsed(BodyType type, void *parsed);
