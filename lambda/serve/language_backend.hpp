/**
 * @file language_backend.hpp
 * @brief Generic language backend interface for multi-language handler dispatch
 *
 * Defines a pluggable interface that allows the web server to execute
 * request handlers written in different languages. Each backend implements
 * five operations: init, execute, compile, needs_recompile, shutdown.
 *
 * Backend implementations:
 *   - Lambda (JIT via MIR)       — primary backend
 *   - JavaScript (MIR JIT)       — handler execution pending
 *
 * Compatible with:
 *   Express:    app.engine('pug', require('pug').__express)
 *   PHP-FPM:    language-specific worker pool model
 */

#pragma once

#include "serve_types.hpp"
#include "http_request.hpp"
#include "http_response.hpp"

// ============================================================================
// Backend Execution Result
// ============================================================================

enum BackendResult {
    BACKEND_OK              = 0,
    BACKEND_ERROR           = -1,
    BACKEND_TIMEOUT         = -2,
    BACKEND_NOT_FOUND       = -3,
    BACKEND_COMPILE_ERROR   = -4
};

// ============================================================================
// Language Backend Interface
// ============================================================================

struct LanguageBackend {
    const char *name;           // backend name: "lambda", "javascript"
    const char **extensions;    // file extensions this backend handles (e.g. {".ls", NULL})
    int extension_count;

    void *backend_data;         // backend-specific state (e.g. Lambda runtime context)

    // lifecycle
    int  (*init)(LanguageBackend *self);
    void (*shutdown)(LanguageBackend *self);

    // execution: runs a handler script/function for the given request
    // returns BackendResult
    int  (*execute)(LanguageBackend *self, const char *handler_path,
                    HttpRequest *req, HttpResponse *resp);

    // optional: pre-compile or cache a handler for faster execution
    int  (*compile)(LanguageBackend *self, const char *handler_path);

    // optional: check if handler needs recompilation (file changed, etc.)
    int  (*needs_recompile)(LanguageBackend *self, const char *handler_path);
};

// ============================================================================
// Backend Registry
// ============================================================================

#define MAX_BACKENDS 8

struct BackendRegistry {
    LanguageBackend *backends[MAX_BACKENDS];
    int count;
};

// create/destroy registry
// backends are external pointers; call backend_registry_shutdown_all() explicitly
// while backend objects are still alive if their lifecycle was initialized.
BackendRegistry* backend_registry_create(void);
void             backend_registry_destroy(BackendRegistry *registry);

// register a backend
int backend_registry_add(BackendRegistry *registry, LanguageBackend *backend);

// find backend by name
LanguageBackend* backend_registry_find(BackendRegistry *registry, const char *name);

// find backend by file extension (e.g. ".ls" → Lambda backend)
LanguageBackend* backend_registry_find_by_ext(BackendRegistry *registry, const char *ext);

// initialize all registered backends
int backend_registry_init_all(BackendRegistry *registry);

// shutdown all registered backends
void backend_registry_shutdown_all(BackendRegistry *registry);
