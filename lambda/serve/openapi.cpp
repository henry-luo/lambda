/**
 * @file openapi.cpp
 * @brief OpenAPI 3.1 specification generator implementation
 */

#include "openapi.hpp"
#include "swagger_ui.hpp"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
#include <cstring>
#include <cstdlib>

// ============================================================================
// Context Lifecycle
// ============================================================================

OpenApiContext* openapi_create(Server *server, const OpenApiConfig *config,
                               EndpointRegistry *registry) {
    if (!server || !config || !registry) return NULL;

    OpenApiContext* ctx = (OpenApiContext*)calloc(1, sizeof(OpenApiContext));
    if (!ctx) return NULL;

    ctx->server = server;
    ctx->config = *config;
    ctx->registry = registry;
    ctx->cached_spec = NULL;

    return ctx;
}

void openapi_destroy(OpenApiContext *ctx) {
    if (!ctx) return;
    free(ctx->cached_spec);
    free(ctx);
}

void openapi_invalidate(OpenApiContext *ctx) {
    if (!ctx) return;
    free(ctx->cached_spec);
    ctx->cached_spec = NULL;
}

// ============================================================================
// JSON Helpers
// ============================================================================

static void json_append_string(StrBuf *buf, const char *key, const char *value) {
    if (!value) return;
    strbuf_append_char(buf, '"');
    strbuf_append_str(buf, key);
    strbuf_append_str(buf, "\":\"");
    // escape basic JSON chars
    for (const char *p = value; *p; p++) {
        switch (*p) {
            case '"':  strbuf_append_str(buf, "\\\""); break;
            case '\\': strbuf_append_str(buf, "\\\\"); break;
            case '\n': strbuf_append_str(buf, "\\n");  break;
            case '\r': strbuf_append_str(buf, "\\r");  break;
            case '\t': strbuf_append_str(buf, "\\t");  break;
            default:   strbuf_append_char(buf, *p);   break;
        }
    }
    strbuf_append_char(buf, '"');
}

static const char* method_string(HttpMethod m) {
    switch (m) {
        case HTTP_GET:     return "get";
        case HTTP_POST:    return "post";
        case HTTP_PUT:     return "put";
        case HTTP_DELETE:  return "delete";
        case HTTP_PATCH:   return "patch";
        case HTTP_OPTIONS: return "options";
        case HTTP_HEAD:    return "head";
        default:           return "get";
    }
}

// convert Express-style :param to OpenAPI {param}
static void append_openapi_path(StrBuf *buf, const char *pattern) {
    for (const char *p = pattern; *p; p++) {
        if (*p == ':') {
            strbuf_append_char(buf, '{');
            p++;
            while (*p && *p != '/' && *p != '?') {
                strbuf_append_char(buf, *p);
                p++;
            }
            strbuf_append_char(buf, '}');
            if (!*p) break;
            strbuf_append_char(buf, *p);
        } else {
            strbuf_append_char(buf, *p);
        }
    }
}

// extract path parameters from Express-style pattern
static void append_path_parameters(StrBuf *buf, const char *pattern, int *first) {
    for (const char *p = pattern; *p; p++) {
        if (*p == ':') {
            if (!*first) strbuf_append_char(buf, ',');
            *first = 0;

            p++;
            const char *name_start = p;
            while (*p && *p != '/' && *p != '?') p++;
            size_t name_len = p - name_start;

            char name[64];
            if (name_len >= sizeof(name)) name_len = sizeof(name) - 1;
            memcpy(name, name_start, name_len);
            name[name_len] = '\0';

            strbuf_append_str(buf, "{\"name\":\"");
            strbuf_append_str(buf, name);
            strbuf_append_str(buf, "\",\"in\":\"path\",\"required\":true,\"schema\":{\"type\":\"string\"}}");

            if (!*p) break;
        }
    }
}

// ============================================================================
// Spec Generation
// ============================================================================

const char* openapi_generate_spec(OpenApiContext *ctx) {
    if (!ctx) return NULL;
    if (ctx->cached_spec) return ctx->cached_spec;

    StrBuf *buf = strbuf_new_cap(4096);

    // openapi version + info
    strbuf_append_str(buf, "{\"openapi\":\"3.1.0\",\"info\":{");
    json_append_string(buf, "title", ctx->config.info.title ? ctx->config.info.title : "API");
    strbuf_append_char(buf, ',');
    json_append_string(buf, "version", ctx->config.info.version ? ctx->config.info.version : "1.0.0");
    if (ctx->config.info.description) {
        strbuf_append_char(buf, ',');
        json_append_string(buf, "description", ctx->config.info.description);
    }
    if (ctx->config.info.terms_of_service) {
        strbuf_append_char(buf, ',');
        json_append_string(buf, "termsOfService", ctx->config.info.terms_of_service);
    }
    if (ctx->config.info.contact_name || ctx->config.info.contact_email) {
        strbuf_append_str(buf, ",\"contact\":{");
        int contact_first = 1;
        if (ctx->config.info.contact_name) {
            json_append_string(buf, "name", ctx->config.info.contact_name);
            contact_first = 0;
        }
        if (ctx->config.info.contact_email) {
            if (!contact_first) strbuf_append_char(buf, ',');
            json_append_string(buf, "email", ctx->config.info.contact_email);
        }
        strbuf_append_char(buf, '}');
    }
    if (ctx->config.info.license_name) {
        strbuf_append_str(buf, ",\"license\":{");
        json_append_string(buf, "name", ctx->config.info.license_name);
        if (ctx->config.info.license_url) {
            strbuf_append_char(buf, ',');
            json_append_string(buf, "url", ctx->config.info.license_url);
        }
        strbuf_append_char(buf, '}');
    }
    strbuf_append_char(buf, '}');

    // servers
    if (ctx->config.base_url) {
        strbuf_append_str(buf, ",\"servers\":[{");
        json_append_string(buf, "url", ctx->config.base_url);
        strbuf_append_str(buf, "}]");
    }

    // paths
    strbuf_append_str(buf, ",\"paths\":{");

    // group endpoints by path
    EndpointRegistry *reg = ctx->registry;
    int path_first = 1;

    // collect unique paths
    const char *paths[MAX_REGISTERED_ENDPOINTS];
    int path_count = 0;

    for (int i = 0; i < reg->count; i++) {
        const char *pat = reg->endpoints[i].pattern;
        int found = 0;
        for (int j = 0; j < path_count; j++) {
            if (strcmp(paths[j], pat) == 0) { found = 1; break; }
        }
        if (!found && path_count < MAX_REGISTERED_ENDPOINTS) {
            paths[path_count++] = pat;
        }
    }

    for (int pi = 0; pi < path_count; pi++) {
        if (!path_first) strbuf_append_char(buf, ',');
        path_first = 0;

        // path key (convert :param to {param})
        strbuf_append_char(buf, '"');
        append_openapi_path(buf, paths[pi]);
        strbuf_append_str(buf, "\":{");

        int method_first = 1;
        for (int i = 0; i < reg->count; i++) {
            if (strcmp(reg->endpoints[i].pattern, paths[pi]) != 0) continue;

            RegisteredEndpoint *ep = &reg->endpoints[i];

            if (!method_first) strbuf_append_char(buf, ',');
            method_first = 0;

            strbuf_append_char(buf, '"');
            strbuf_append_str(buf, method_string(ep->method));
            strbuf_append_str(buf, "\":{");

            // summary
            if (ep->meta.summary) {
                json_append_string(buf, "summary", ep->meta.summary);
            } else {
                json_append_string(buf, "summary", "");
            }

            // description
            if (ep->meta.description) {
                strbuf_append_char(buf, ',');
                json_append_string(buf, "description", ep->meta.description);
            }

            // deprecated
            if (ep->meta.deprecated) {
                strbuf_append_str(buf, ",\"deprecated\":true");
            }

            // tags
            if (ep->meta.tags) {
                strbuf_append_str(buf, ",\"tags\":[");
                const char *t = ep->meta.tags;
                int tag_first = 1;
                while (*t) {
                    while (*t == ' ' || *t == ',') t++;
                    if (!*t) break;
                    if (!tag_first) strbuf_append_char(buf, ',');
                    tag_first = 0;
                    strbuf_append_char(buf, '"');
                    while (*t && *t != ',') {
                        // trim trailing spaces
                        const char *end = t;
                        while (*end && *end != ',') end++;
                        while (end > t && *(end-1) == ' ') end--;
                        while (t < end) {
                            strbuf_append_char(buf, *t);
                            t++;
                        }
                        while (*t && *t != ',') t++;
                    }
                    strbuf_append_char(buf, '"');
                }
                strbuf_append_char(buf, ']');
            }

            // path parameters
            int has_params = 0;
            for (const char *pp = paths[pi]; *pp; pp++) {
                if (*pp == ':') { has_params = 1; break; }
            }
            if (has_params) {
                strbuf_append_str(buf, ",\"parameters\":[");
                int param_first = 1;
                append_path_parameters(buf, paths[pi], &param_first);
                strbuf_append_char(buf, ']');
            }

            // requestBody (only for POST/PUT/PATCH)
            if (ep->meta.request_schema &&
                (ep->method == HTTP_POST || ep->method == HTTP_PUT || ep->method == HTTP_PATCH)) {
                strbuf_append_str(buf, ",\"requestBody\":{\"required\":true,"
                    "\"content\":{\"application/json\":{\"schema\":");
                strbuf_append_str(buf, ep->meta.request_schema);
                strbuf_append_str(buf, "}}}");
            }

            // responses
            strbuf_append_str(buf, ",\"responses\":{\"200\":{\"description\":\"Success\"");
            if (ep->meta.response_schema) {
                strbuf_append_str(buf, ",\"content\":{\"application/json\":{\"schema\":");
                strbuf_append_str(buf, ep->meta.response_schema);
                strbuf_append_str(buf, "}}");
            }
            strbuf_append_str(buf, "}}");

            strbuf_append_char(buf, '}'); // end method object
        }

        strbuf_append_char(buf, '}'); // end path object
    }

    strbuf_append_str(buf, "}}"); // close paths + root

    // cache result
    size_t len = buf->length;
    ctx->cached_spec = (char*)malloc(len + 1);
    memcpy(ctx->cached_spec, buf->str, len + 1);

    strbuf_free(buf);
    return ctx->cached_spec;
}

// ============================================================================
// Serve Spec + Swagger UI
// ============================================================================

static void handle_openapi_json(HttpRequest* req, HttpResponse* resp, void* user_data) {
    OpenApiContext *ctx = (OpenApiContext*)user_data;
    const char *spec = openapi_generate_spec(ctx);
    if (spec) {
        http_response_status(resp, 200);
        http_response_set_header(resp, "Content-Type", "application/json; charset=utf-8");
        http_response_set_header(resp, "Access-Control-Allow-Origin", "*");
        http_response_write_str(resp, spec);
    } else {
        http_response_status(resp, 500);
        http_response_write_str(resp, "{\"error\":\"Failed to generate spec\"}");
    }
}

int openapi_serve(OpenApiContext *ctx) {
    if (!ctx || !ctx->server) return -1;

    server_get(ctx->server, "/openapi.json", handle_openapi_json, ctx);
    swagger_ui_serve(ctx->server, "/docs", "/openapi.json");

    log_info("openapi: serving spec at /openapi.json and docs at /docs");
    return 0;
}
