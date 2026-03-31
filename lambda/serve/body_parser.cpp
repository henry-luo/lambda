/**
 * @file body_parser.cpp
 * @brief Request body parsing — JSON and URL-encoded form data
 */

#include "body_parser.hpp"
#include "serve_utils.hpp"
#include <string.h>
#include <ctype.h>

// ============================================================================
// JSON Validation (lightweight — checks structure, not full RFC 8259)
// ============================================================================

static int validate_json_structure(const char *data, size_t len) {
    if (!data || len == 0) return 0;

    // skip leading whitespace
    size_t i = 0;
    while (i < len && isspace((unsigned char)data[i])) i++;
    if (i >= len) return 0;

    // must start with { or [
    char first = data[i];
    if (first != '{' && first != '[') return 0;

    // find matching close bracket (simple depth counting)
    int depth = 0;
    int in_string = 0;
    int escaped = 0;

    for (; i < len; i++) {
        char c = data[i];

        if (escaped) {
            escaped = 0;
            continue;
        }
        if (c == '\\' && in_string) {
            escaped = 1;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;

        if (c == '{' || c == '[') depth++;
        if (c == '}' || c == ']') depth--;

        if (depth == 0) return 1; // matched
    }

    return 0; // unbalanced
}

// ============================================================================
// JSON Body Parser
// ============================================================================

ParsedJson* body_parse_json(const char *data, size_t len) {
    if (!data || len == 0) return NULL;

    if (!validate_json_structure(data, len)) {
        serve_set_error("body_parse_json: invalid JSON structure");
        return NULL;
    }

    ParsedJson *pj = (ParsedJson *)serve_calloc(1, sizeof(ParsedJson));
    if (!pj) return NULL;

    pj->json_str = (char *)serve_malloc(len + 1);
    if (!pj->json_str) {
        serve_free(pj);
        return NULL;
    }

    memcpy(pj->json_str, data, len);
    pj->json_str[len] = '\0';
    pj->len = len;
    return pj;
}

// ============================================================================
// URL-Encoded Form Body Parser
// ============================================================================

HttpHeader* body_parse_form(const char *data, size_t len) {
    if (!data || len == 0) return NULL;

    HttpHeader *list = NULL;
    const char *p = data;
    const char *end = data + len;

    while (p < end) {
        // find key=value delimiter
        const char *key_start = p;
        const char *eq = NULL;
        const char *pair_end = p;

        while (pair_end < end && *pair_end != '&') {
            if (*pair_end == '=' && !eq) eq = pair_end;
            pair_end++;
        }

        if (eq) {
            size_t key_len = (size_t)(eq - key_start);
            size_t val_len = (size_t)(pair_end - eq - 1);

            char *key = (char *)serve_malloc(key_len + 1);
            char *val = (char *)serve_malloc(val_len + 1);

            if (key && val) {
                memcpy(key, key_start, key_len);
                key[key_len] = '\0';
                serve_url_decode(key);

                memcpy(val, eq + 1, val_len);
                val[val_len] = '\0';
                serve_url_decode(val);

                list = http_header_add(list, key, val);
            }

            serve_free(key);
            serve_free(val);
        }

        p = (pair_end < end) ? pair_end + 1 : pair_end;
    }

    return list;
}

// ============================================================================
// Dispatch: parse body based on Content-Type
// ============================================================================

int body_parse(HttpRequest *req) {
    if (!req || !req->body || req->body_len == 0) return 0;
    if (req->parsed_body_type != BODY_NONE) return 0; // already parsed

    const char *ct = http_header_find(req->headers, "Content-Type");
    if (!ct) return 0;

    if (serve_strcasecmp(ct, "application/json") == 0 ||
        strstr(ct, "application/json") != NULL) {
        ParsedJson *pj = body_parse_json(req->body, req->body_len);
        if (pj) {
            req->parsed_body = pj;
            req->parsed_body_type = BODY_JSON;
            return 0;
        }
        return -1;
    }

    if (serve_strcasecmp(ct, "application/x-www-form-urlencoded") == 0 ||
        strstr(ct, "x-www-form-urlencoded") != NULL) {
        HttpHeader *form = body_parse_form(req->body, req->body_len);
        if (form) {
            req->parsed_body = form;
            req->parsed_body_type = BODY_FORM;
            return 0;
        }
        return -1;
    }

    // unsupported content type — leave as raw
    req->parsed_body_type = BODY_RAW;
    return 0;
}

// ============================================================================
// Cleanup
// ============================================================================

void body_free_parsed(BodyType type, void *parsed) {
    if (!parsed) return;

    switch (type) {
        case BODY_JSON: {
            ParsedJson *pj = (ParsedJson *)parsed;
            serve_free(pj->json_str);
            serve_free(pj);
            break;
        }
        case BODY_FORM: {
            http_header_free((HttpHeader *)parsed);
            break;
        }
        default:
            break;
    }
}
