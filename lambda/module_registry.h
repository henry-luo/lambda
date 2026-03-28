// module_registry.h — Unified cross-language module registry
// Provides a single registry for modules compiled from any language (Lambda, JS, Python, etc.)
// that runs on the Lambda runtime. Modules are keyed by resolved absolute path.
#pragma once

#ifdef __cplusplus
#include "lambda.hpp"
#else
#include "lambda.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Module descriptor — one per loaded module
typedef struct ModuleDescriptor {
    const char* path;           // resolved absolute path (unique key, owned)
    const char* source_lang;    // "lambda", "js", "python", ... (static string, not owned)
    Item namespace_obj;         // namespace Item (map of exported symbols)
    void* mir_ctx;              // compiled MIR context (opaque, for function lookup)
    bool initialized;           // true after module code has executed
    bool loading;               // true while module is being loaded (circular import detection)
} ModuleDescriptor;

// Initialize the module registry (call once at startup)
void module_registry_init(void);

// Clean up the module registry
void module_registry_cleanup(void);

// Register a compiled + executed module
void module_register(const char* path, const char* lang, Item namespace_obj, void* mir_ctx);

// Look up a module by resolved path. Returns NULL if not found.
ModuleDescriptor* module_get(const char* path);

// Check if a module is already loaded
bool module_is_loaded(const char* path);

// Mark a module as currently loading (for circular import detection).
// Creates a partial entry with loading=true, initialized=false, empty namespace.
// Returns the descriptor so the caller can set namespace_obj after execution.
ModuleDescriptor* module_register_loading(const char* path, const char* lang);

// Check if a module is currently being loaded (circular import in progress)
bool module_is_loading(const char* path);

// Build a namespace Item (map) from a compiled Lambda module's public API.
// Walks the AST for pub functions and variables, wrapping compiled function
// pointers as Function Items and reading variable values from the module struct.
// Returns the namespace Item (type=MAP).
Item module_build_lambda_namespace(void* script_ptr);

// Create a synthetic Script from a JS namespace object for Lambda→JS import.
// Walks the JS namespace map's shape entries and creates synthetic AST nodes
// (AstFuncNode/AstNamedNode) so the Lambda import system can bind them.
// The returned Script has ast_root, reference, and is registered in runtime->scripts.
// Returns NULL on error.
void* create_js_import_script(const char* resolved_path, Item namespace_obj, void* runtime_ptr);

// ---------------------------------------------------------------------------
// Naming convention helpers (Phase 4)
// ---------------------------------------------------------------------------
// Lambda MIR names: user functions → _name_offset (e.g., _add_42)
//                   system funcs  → name          (e.g., len, type)
// Namespace keys:   raw user name without _ prefix (e.g., "add")
// The _ prefix is applied by write_fn_name at the MIR level.

// Convert raw export name to MIR-style unified name: "add" → "_add".
// Writes into caller-provided buf. Returns buf.
const char* module_to_mir_name(const char* raw_name, char* buf, int buf_size);

// Convert MIR-style name to raw export name: "_add" → "add".
// Returns pointer into unified_name (no allocation).
const char* module_from_mir_name(const char* unified_name);

// Check if a raw export name collides with a system function.
// Returns true if the name is reserved (e.g., "len", "type", "map").
bool module_name_collides_with_sys(const char* name, int name_len);

#ifdef __cplusplus
}
#endif
