/**
 * @file rest.cpp
 * @brief REST resource registration implementation
 */

#include "rest.hpp"
#include "server.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cstdlib>

// ============================================================================
// Endpoint Registry
// ============================================================================

EndpointRegistry* rest_registry_create(void) {
    EndpointRegistry* reg = (EndpointRegistry*)calloc(1, sizeof(EndpointRegistry));
    return reg;
}

void rest_registry_destroy(EndpointRegistry *reg) {
    if (!reg) return;
    // patterns and schema strings are caller-owned, not freed here
    free(reg);
}

static int registry_add(EndpointRegistry *reg, HttpMethod method,
                         const char *pattern, const RestEndpoint *meta) {
    if (!reg || reg->count >= MAX_REGISTERED_ENDPOINTS) return -1;

    RegisteredEndpoint *ep = &reg->endpoints[reg->count];
    ep->method = method;
    ep->pattern = pattern;
    if (meta) {
        ep->meta = *meta;
    } else {
        memset(&ep->meta, 0, sizeof(RestEndpoint));
    }
    reg->count++;
    return 0;
}

// ============================================================================
// REST Resource Registration
// ============================================================================

int server_rest_resource(Server *server, const RestResource *resource,
                         EndpointRegistry *registry) {
    if (!server || !resource || !resource->base_path) return -1;

    // build /:id pattern
    char id_pattern[256];
    snprintf(id_pattern, sizeof(id_pattern), "%s/:id", resource->base_path);

    // default tag from resource name
    const char *tag = resource->tag ? resource->tag : resource->name;

    // helper to fill default summary if not provided
    #define REGISTER_CRUD(method_fn, handler, pattern, meta_field, default_summary, http_method) \
        do { \
            if (handler) { \
                method_fn(server, pattern, handler, resource->handler_data); \
                if (registry) { \
                    RestEndpoint ep_meta = resource->meta_field; \
                    if (!ep_meta.summary) ep_meta.summary = default_summary; \
                    if (!ep_meta.tags && tag) ep_meta.tags = tag; \
                    registry_add(registry, http_method, pattern, &ep_meta); \
                } \
            } \
        } while (0)

    REGISTER_CRUD(server_get,   resource->list_handler,   resource->base_path, list_meta,
                  "List all", HTTP_GET);
    REGISTER_CRUD(server_post,  resource->create_handler, resource->base_path, create_meta,
                  "Create new", HTTP_POST);
    REGISTER_CRUD(server_get,   resource->get_handler,    id_pattern, get_meta,
                  "Get by ID", HTTP_GET);
    REGISTER_CRUD(server_put,   resource->update_handler, id_pattern, update_meta,
                  "Update by ID", HTTP_PUT);
    REGISTER_CRUD(server_patch, resource->patch_handler,  id_pattern, patch_meta,
                  "Partial update", HTTP_PATCH);
    REGISTER_CRUD(server_del,   resource->delete_handler, id_pattern, delete_meta,
                  "Delete by ID", HTTP_DELETE);

    #undef REGISTER_CRUD

    log_info("rest: registered resource '%s' at %s", resource->name, resource->base_path);
    return 0;
}

// ============================================================================
// Single Endpoint Registration
// ============================================================================

int server_rest_endpoint(Server *server, HttpMethod method, const char *pattern,
                         RequestHandler handler, void *user_data,
                         const RestEndpoint *meta, EndpointRegistry *registry) {
    if (!server || !pattern || !handler) return -1;

    // register route based on method
    switch (method) {
        case HTTP_GET:     server_get(server, pattern, handler, user_data);     break;
        case HTTP_POST:    server_post(server, pattern, handler, user_data);    break;
        case HTTP_PUT:     server_put(server, pattern, handler, user_data);     break;
        case HTTP_DELETE:  server_del(server, pattern, handler, user_data);     break;
        case HTTP_PATCH:   server_patch(server, pattern, handler, user_data);   break;
        case HTTP_OPTIONS: server_options(server, pattern, handler, user_data); break;
        default:
            server_all(server, pattern, handler, user_data);
            break;
    }

    if (registry && meta) {
        registry_add(registry, method, pattern, meta);
    }

    return 0;
}
