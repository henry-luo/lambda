/**
 * @file schema_validator.cpp
 * @brief JSON Schema draft 2020-12 subset validator implementation
 *
 * Validates JSON data against a JSON Schema. Works with raw JSON strings
 * using minimal parsing (no dependency on external JSON libraries).
 */

#include "schema_validator.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include "../../lib/mem.h"
#include <cstdio>
#include <cmath>
#include <cctype>

// ============================================================================
// Minimal JSON Value Types (for validation only)
// ============================================================================

typedef enum JsonType {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

// ============================================================================
// JSON Parsing Helpers
// ============================================================================

static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static JsonType detect_type(const char* p) {
    p = skip_ws(p);
    if (*p == '"') return JSON_STRING;
    if (*p == '{') return JSON_OBJECT;
    if (*p == '[') return JSON_ARRAY;
    if (*p == 't' || *p == 'f') return JSON_BOOL;
    if (*p == 'n') return JSON_NULL;
    if (*p == '-' || isdigit((unsigned char)*p)) return JSON_NUMBER;
    return JSON_NULL;
}

// skip a JSON value and return pointer past it
static const char* skip_value(const char* p) {
    p = skip_ws(p);
    switch (*p) {
        case '"': {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\') p++;
                p++;
            }
            if (*p == '"') p++;
            return p;
        }
        case '{': {
            p++;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '{') depth++;
                else if (*p == '}') depth--;
                else if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\') p++; p++; } }
                p++;
            }
            return p;
        }
        case '[': {
            p++;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                else if (*p == '"') { p++; while (*p && *p != '"') { if (*p == '\\') p++; p++; } }
                p++;
            }
            return p;
        }
        case 't': return p + 4; // true
        case 'f': return p + 5; // false
        case 'n': return p + 4; // null
        default: {
            // number
            if (*p == '-') p++;
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
            if (*p == 'e' || *p == 'E') { p++; if (*p == '+' || *p == '-') p++; while (isdigit((unsigned char)*p)) p++; }
            return p;
        }
    }
}

// extract a string value (returns pointer to content, writes to buf)
static int extract_string_value(const char* p, char* buf, size_t buf_size) {
    p = skip_ws(p);
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < buf_size - 1) {
        if (*p == '\\') { p++; if (!*p) break; }
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return 1;
}

// extract a number value
static double extract_number(const char* p) {
    p = skip_ws(p);
    return strtod(p, NULL);
}

// check if a number is an integer
static int is_integer_value(const char* p) {
    p = skip_ws(p);
    if (*p == '-') p++;
    while (isdigit((unsigned char)*p)) p++;
    p = skip_ws(p);
    return (*p == ',' || *p == '}' || *p == ']' || *p == '\0');
}

// count array elements
static int count_array_items(const char* p) {
    p = skip_ws(p);
    if (*p != '[') return 0;
    p = skip_ws(p + 1);
    if (*p == ']') return 0;
    int count = 1;
    while (*p) {
        p = skip_value(p);
        p = skip_ws(p);
        if (*p == ',') { count++; p++; }
        else break;
    }
    return count;
}

// find key in object, return pointer to the value
static const char* find_object_key(const char* obj, const char* key) {
    const char* p = skip_ws(obj);
    if (*p != '{') return NULL;
    p = skip_ws(p + 1);
    if (*p == '}') return NULL;

    while (*p) {
        if (*p != '"') break;
        // parse key
        p++;
        const char* key_start = p;
        while (*p && *p != '"') { if (*p == '\\') p++; p++; }
        size_t klen = p - key_start;
        if (*p == '"') p++;

        p = skip_ws(p);
        if (*p == ':') p++;
        p = skip_ws(p);

        // check if this is the key we want
        if (klen == strlen(key) && strncmp(key_start, key, klen) == 0) {
            return p;
        }

        // skip value
        p = skip_value(p);
        p = skip_ws(p);
        if (*p == ',') p = skip_ws(p + 1);
        else break;
    }
    return NULL;
}

// iterate object keys
typedef void (*ObjKeyCallback)(const char* key, size_t key_len, const char* value, void* ctx);

static void iterate_object(const char* obj, ObjKeyCallback cb, void* ctx) {
    const char* p = skip_ws(obj);
    if (*p != '{') return;
    p = skip_ws(p + 1);
    if (*p == '}') return;

    while (*p) {
        if (*p != '"') break;
        p++;
        const char* key_start = p;
        while (*p && *p != '"') { if (*p == '\\') p++; p++; }
        size_t klen = p - key_start;
        if (*p == '"') p++;

        p = skip_ws(p);
        if (*p == ':') p++;
        p = skip_ws(p);

        cb(key_start, klen, p, ctx);

        p = skip_value(p);
        p = skip_ws(p);
        if (*p == ',') p = skip_ws(p + 1);
        else break;
    }
}

// ============================================================================
// Validation Error Recording
// ============================================================================

static void add_error(ValidationResult *result, const char *path, const char *message) {
    if (result->error_count >= MAX_VALIDATION_ERRORS) return;
    result->errors[result->error_count].path = path;
    result->errors[result->error_count].message = message;
    result->error_count++;
    result->valid = 0;
}

// ============================================================================
// Schema Validation Core
// ============================================================================

static void validate_value(const char *data, const char *schema,
                           const char *path, ValidationResult *result);

// check type constraint
static int check_type(const char *type_val, JsonType actual) {
    char type_str[32];
    if (!extract_string_value(type_val, type_str, sizeof(type_str))) return 1;

    switch (actual) {
        case JSON_STRING:  return strcmp(type_str, "string") == 0;
        case JSON_NUMBER:  return strcmp(type_str, "number") == 0 || strcmp(type_str, "integer") == 0;
        case JSON_BOOL:    return strcmp(type_str, "boolean") == 0;
        case JSON_NULL:    return strcmp(type_str, "null") == 0;
        case JSON_ARRAY:   return strcmp(type_str, "array") == 0;
        case JSON_OBJECT:  return strcmp(type_str, "object") == 0;
    }
    return 0;
}

// validate string constraints
static void validate_string(const char *data, const char *schema,
                             const char *path, ValidationResult *result) {
    char str_val[4096];
    if (!extract_string_value(data, str_val, sizeof(str_val))) return;

    size_t len = strlen(str_val);

    const char *min_len = find_object_key(schema, "minLength");
    if (min_len) {
        int min = (int)extract_number(min_len);
        if ((int)len < min) {
            add_error(result, path, "string too short");
        }
    }

    const char *max_len = find_object_key(schema, "maxLength");
    if (max_len) {
        int max = (int)extract_number(max_len);
        if ((int)len > max) {
            add_error(result, path, "string too long");
        }
    }
}

// validate numeric constraints
static void validate_number(const char *data, const char *schema,
                             const char *path, ValidationResult *result) {
    double val = extract_number(data);

    // check integer type
    const char *type_val = find_object_key(schema, "type");
    if (type_val) {
        char type_str[32];
        if (extract_string_value(type_val, type_str, sizeof(type_str)) &&
            strcmp(type_str, "integer") == 0) {
            if (!is_integer_value(data)) {
                add_error(result, path, "expected integer");
            }
        }
    }

    const char *minimum = find_object_key(schema, "minimum");
    if (minimum) {
        double min = extract_number(minimum);
        if (val < min) {
            add_error(result, path, "value below minimum");
        }
    }

    const char *maximum = find_object_key(schema, "maximum");
    if (maximum) {
        double max = extract_number(maximum);
        if (val > max) {
            add_error(result, path, "value above maximum");
        }
    }

    const char *multiple = find_object_key(schema, "multipleOf");
    if (multiple) {
        double m = extract_number(multiple);
        if (m > 0 && fmod(val, m) != 0.0) {
            add_error(result, path, "not a multiple of required value");
        }
    }
}

// validate enum
static int validate_enum(const char *data, const char *enum_val) {
    const char *p = skip_ws(enum_val);
    if (*p != '[') return 1;
    p = skip_ws(p + 1);
    if (*p == ']') return 0; // empty enum = nothing matches

    JsonType data_type = detect_type(data);

    while (*p) {
        const char *item_start = p;
        const char *item_end = skip_value(p);

        // compare values (simplified: string comparison of JSON repr)
        if (data_type == detect_type(item_start)) {
            if (data_type == JSON_STRING) {
                char data_str[1024], enum_str[1024];
                if (extract_string_value(data, data_str, sizeof(data_str)) &&
                    extract_string_value(item_start, enum_str, sizeof(enum_str)) &&
                    strcmp(data_str, enum_str) == 0) {
                    return 1;
                }
            } else {
                // compare raw JSON
                size_t data_len = skip_value(data) - data;
                size_t item_len = item_end - item_start;
                if (data_len == item_len && strncmp(data, item_start, data_len) == 0) {
                    return 1;
                }
            }
        }

        p = skip_ws(item_end);
        if (*p == ',') p = skip_ws(p + 1);
        else break;
    }
    return 0;
}

// validate array
static void validate_array(const char *data, const char *schema,
                            const char *path, ValidationResult *result) {
    int count = count_array_items(data);

    const char *min_items = find_object_key(schema, "minItems");
    if (min_items) {
        int min = (int)extract_number(min_items);
        if (count < min) {
            add_error(result, path, "too few array items");
        }
    }

    const char *max_items = find_object_key(schema, "maxItems");
    if (max_items) {
        int max = (int)extract_number(max_items);
        if (count > max) {
            add_error(result, path, "too many array items");
        }
    }

    // validate items schema
    const char *items_schema = find_object_key(schema, "items");
    if (items_schema && count > 0) {
        const char *p = skip_ws(data);
        if (*p == '[') {
            p = skip_ws(p + 1);
            for (int i = 0; i < count && *p && *p != ']'; i++) {
                char item_path[256];
                snprintf(item_path, sizeof(item_path), "%s/%d", path, i);
                validate_value(p, items_schema, item_path, result);
                p = skip_value(p);
                p = skip_ws(p);
                if (*p == ',') p = skip_ws(p + 1);
            }
        }
    }
}

// context for required fields check
typedef struct {
    const char **required_keys;
    int *found;
    int count;
} RequiredCtx;

static void check_required_key(const char* key, size_t key_len, const char* value, void* ctx) {
    RequiredCtx *rctx = (RequiredCtx*)ctx;
    for (int i = 0; i < rctx->count; i++) {
        if (strlen(rctx->required_keys[i]) == key_len &&
            strncmp(rctx->required_keys[i], key, key_len) == 0) {
            rctx->found[i] = 1;
        }
    }
}

// validate object
static void validate_object(const char *data, const char *schema,
                             const char *path, ValidationResult *result) {
    // check required fields
    const char *required = find_object_key(schema, "required");
    if (required) {
        const char *p = skip_ws(required);
        if (*p == '[') {
            p = skip_ws(p + 1);
            const char *req_keys[64];
            int req_found[64];
            int req_count = 0;

            while (*p && *p != ']' && req_count < 64) {
                char key_buf[256];
                if (extract_string_value(p, key_buf, sizeof(key_buf))) {
                    req_keys[req_count] = mem_strdup(key_buf, MEM_CAT_SERVE);
                    req_found[req_count] = 0;
                    req_count++;
                }
                p = skip_value(p);
                p = skip_ws(p);
                if (*p == ',') p = skip_ws(p + 1);
            }

            // check which required keys are present in data
            RequiredCtx rctx = { req_keys, req_found, req_count };
            iterate_object(data, check_required_key, &rctx);

            for (int i = 0; i < req_count; i++) {
                if (!req_found[i]) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "missing required field '%s'", req_keys[i]);
                    char err_path[256];
                    snprintf(err_path, sizeof(err_path), "%s/%s", path, req_keys[i]);
                    add_error(result, mem_strdup(err_path, MEM_CAT_SERVE), mem_strdup(msg, MEM_CAT_SERVE));
                }
                mem_free((void*)req_keys[i]);
            }
        }
    }

    // validate properties
    const char *properties = find_object_key(schema, "properties");
    if (properties) {
        // iterate data keys and validate against property schemas
        const char *dp = skip_ws(data);
        if (*dp == '{') {
            dp = skip_ws(dp + 1);
            while (*dp && *dp != '}') {
                if (*dp != '"') break;
                dp++;
                const char *key_start = dp;
                while (*dp && *dp != '"') { if (*dp == '\\') dp++; dp++; }
                size_t klen = dp - key_start;
                if (*dp == '"') dp++;

                dp = skip_ws(dp);
                if (*dp == ':') dp++;
                dp = skip_ws(dp);

                char key_buf[256];
                if (klen < sizeof(key_buf)) {
                    memcpy(key_buf, key_start, klen);
                    key_buf[klen] = '\0';

                    const char *prop_schema = find_object_key(properties, key_buf);
                    if (prop_schema) {
                        char prop_path[256];
                        snprintf(prop_path, sizeof(prop_path), "%s/%s", path, key_buf);
                        validate_value(dp, prop_schema, prop_path, result);
                    }
                }

                dp = skip_value(dp);
                dp = skip_ws(dp);
                if (*dp == ',') dp = skip_ws(dp + 1);
                else break;
            }
        }
    }
}

static void validate_value(const char *data, const char *schema,
                           const char *path, ValidationResult *result) {
    data = skip_ws(data);
    schema = skip_ws(schema);

    if (!*data || !*schema) return;

    JsonType actual_type = detect_type(data);

    // type check
    const char *type_val = find_object_key(schema, "type");
    if (type_val) {
        if (!check_type(type_val, actual_type)) {
            add_error(result, path, "type mismatch");
            return;
        }
    }

    // enum check
    const char *enum_val = find_object_key(schema, "enum");
    if (enum_val) {
        if (!validate_enum(data, enum_val)) {
            add_error(result, path, "value not in enum");
        }
    }

    // type-specific validation
    switch (actual_type) {
        case JSON_STRING:
            validate_string(data, schema, path, result);
            break;
        case JSON_NUMBER:
            validate_number(data, schema, path, result);
            break;
        case JSON_ARRAY:
            validate_array(data, schema, path, result);
            break;
        case JSON_OBJECT:
            validate_object(data, schema, path, result);
            break;
        default:
            break;
    }
}

// ============================================================================
// Public API
// ============================================================================

int schema_validate(const char *json_data, const char *schema_json,
                    ValidationResult *result) {
    if (!result) return 0;

    result->valid = 1;
    result->error_count = 0;

    if (!json_data || !schema_json) {
        add_error(result, "/", "null data or schema");
        return 0;
    }

    validate_value(json_data, schema_json, "", result);
    return result->valid;
}

char* schema_validation_error_json(const ValidationResult *result) {
    if (!result) return NULL;

    StrBuf *buf = strbuf_new_cap(512);
    strbuf_append_str(buf, "{\"error\":\"validation_error\","
                           "\"message\":\"Request body validation failed\","
                           "\"details\":[");

    for (int i = 0; i < result->error_count; i++) {
        if (i > 0) strbuf_append_char(buf, ',');
        strbuf_append_str(buf, "{\"path\":\"");
        if (result->errors[i].path) {
            strbuf_append_str(buf, result->errors[i].path);
        }
        strbuf_append_str(buf, "\",\"message\":\"");
        if (result->errors[i].message) {
            strbuf_append_str(buf, result->errors[i].message);
        }
        strbuf_append_str(buf, "\"}");
    }

    strbuf_append_str(buf, "]}");

    size_t len = buf->length;
    char *json = (char*)mem_alloc(len + 1, MEM_CAT_SERVE);
    memcpy(json, buf->str, len + 1);
    strbuf_free(buf);

    return json;
}

// ============================================================================
// Validation Middleware
// ============================================================================

static void schema_validate_middleware_fn(HttpRequest* req, HttpResponse* resp,
                                          void* user_data, MiddlewareContext* ctx) {
    EndpointRegistry *registry = (EndpointRegistry*)user_data;

    // only validate POST/PUT/PATCH with bodies
    if (req->method != HTTP_POST && req->method != HTTP_PUT && req->method != HTTP_PATCH) {
        middleware_next(ctx);
        return;
    }

    // find matching endpoint in registry
    const char *schema_json = NULL;
    if (registry && req->path) {
        for (int i = 0; i < registry->count; i++) {
            RegisteredEndpoint *ep = &registry->endpoints[i];
            if (ep->method == req->method && ep->meta.request_schema) {
                // simple path match — the router has already matched, so just compare the pattern
                // a more robust approach would use the router's match info
                if (strcmp(ep->pattern, req->path) == 0) {
                    schema_json = ep->meta.request_schema;
                    break;
                }
            }
        }
    }

    if (!schema_json) {
        middleware_next(ctx);
        return;
    }

    const char *body = http_request_body(req);
    if (!body || !body[0]) {
        http_response_status(resp, 422);
        http_response_set_header(resp, "Content-Type", "application/json");
        http_response_write_str(resp, "{\"error\":\"validation_error\","
                                      "\"message\":\"Request body is empty\","
                                      "\"details\":[]}");
        return;
    }

    ValidationResult result;
    if (!schema_validate(body, schema_json, &result)) {
        char *error_json = schema_validation_error_json(&result);
        http_response_status(resp, 422);
        http_response_set_header(resp, "Content-Type", "application/json");
        http_response_write_str(resp, error_json);
        mem_free(error_json);
        return;
    }

    middleware_next(ctx);
}

MiddlewareFn schema_validation_middleware(void) {
    return schema_validate_middleware_fn;
}
