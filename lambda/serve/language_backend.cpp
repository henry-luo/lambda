/**
 * @file language_backend.cpp
 * @brief Backend registry implementation
 */

#include "language_backend.hpp"
#include "serve_utils.hpp"
#include "../../lib/log.h"

#include <string.h>

// ============================================================================
// Registry lifecycle
// ============================================================================

BackendRegistry* backend_registry_create(void) {
    BackendRegistry *reg = (BackendRegistry*)serve_calloc(1, sizeof(BackendRegistry));
    return reg;
}

void backend_registry_destroy(BackendRegistry *registry) {
    if (!registry) return;
    backend_registry_shutdown_all(registry);
    serve_free(registry);
}

// ============================================================================
// Registration
// ============================================================================

int backend_registry_add(BackendRegistry *registry, LanguageBackend *backend) {
    if (!registry || !backend) return -1;
    if (registry->count >= MAX_BACKENDS) {
        log_error("backend registry full, cannot add '%s'", backend->name);
        return -1;
    }
    // check for duplicate name
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->backends[i]->name, backend->name) == 0) {
            log_error("backend '%s' already registered", backend->name);
            return -1;
        }
    }
    registry->backends[registry->count++] = backend;
    log_info("registered backend '%s' (%d extensions)", backend->name, backend->extension_count);
    return 0;
}

// ============================================================================
// Lookup
// ============================================================================

LanguageBackend* backend_registry_find(BackendRegistry *registry, const char *name) {
    if (!registry || !name) return NULL;
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->backends[i]->name, name) == 0) {
            return registry->backends[i];
        }
    }
    return NULL;
}

LanguageBackend* backend_registry_find_by_ext(BackendRegistry *registry, const char *ext) {
    if (!registry || !ext) return NULL;
    for (int i = 0; i < registry->count; i++) {
        LanguageBackend *b = registry->backends[i];
        for (int j = 0; j < b->extension_count; j++) {
            if (strcmp(b->extensions[j], ext) == 0) {
                return b;
            }
        }
    }
    return NULL;
}

// ============================================================================
// Lifecycle for all backends
// ============================================================================

int backend_registry_init_all(BackendRegistry *registry) {
    if (!registry) return -1;
    for (int i = 0; i < registry->count; i++) {
        LanguageBackend *b = registry->backends[i];
        if (b->init) {
            int result = b->init(b);
            if (result != 0) {
                log_error("failed to initialize backend '%s'", b->name);
                return result;
            }
            log_info("initialized backend '%s'", b->name);
        }
    }
    return 0;
}

void backend_registry_shutdown_all(BackendRegistry *registry) {
    if (!registry) return;
    for (int i = 0; i < registry->count; i++) {
        LanguageBackend *b = registry->backends[i];
        if (b->shutdown) {
            b->shutdown(b);
            log_info("shutdown backend '%s'", b->name);
        }
    }
}
