// module_registry.h — Unified cross-language module registry
// Provides a single registry for modules compiled from any language (Lambda, JS, Python, etc.)
// that runs on the Lambda runtime. Modules are keyed by resolved absolute path.
#pragma once

#ifdef __cplusplus
#include "../lambda.hpp"
#include "ast-core.hpp"
#else
#include "../lambda.h"
typedef struct LangProfile LangProfile;
#endif

typedef struct Runtime Runtime;
typedef struct ModuleRegistry ModuleRegistry;

#ifdef __cplusplus
extern "C" {
#endif

// A namespace stays language-owned. The registry uses this narrow membrane
// only to enumerate already-published exports for a cross-language import.
typedef struct ModuleNamespaceOps {
    Item (*create)(void);
    Item (*get)(Item namespace_obj, const char* name);
    int (*function_arity)(Item function_obj);
    void* (*function_ptr)(Item function_obj);
} ModuleNamespaceOps;

// Module descriptor — one per loaded module
typedef struct ModuleDescriptor {
    struct ModuleDescriptor* next; // runtime-owned descriptor order
    const char* path;           // resolved absolute path (unique key, owned)
    const char* source_lang;    // "lambda", "js", "python", ... (static string, not owned)
    LangProfile* profile;       // resolved language profile for shared AST phases
    Item namespace_obj;         // namespace Item (map of exported symbols)
    const ModuleNamespaceOps* namespace_ops; // language-owned export membrane
    void* mir_ctx;              // compiled MIR context (opaque, for function lookup)
    bool initialized;           // true after module code has executed
    bool loading;               // true while module is being loaded (circular import detection)

    // JS module evaluation state belongs to the same canonical descriptor as
    // its namespace. Other languages leave these fields at their zero values.
    Item specifier_item;
    Item awaited_target;
    Item evaluation_error;
    int has_tla;
    int pending_async_deps;
    int async_parent_count;
    int async_parent_capacity;
    struct ModuleDescriptor** async_parents;
    void* deferred_main_ptr;
    int body_executed;
    int post_await_pending;
    int body_state;
    int async_eval_order;
    uint32_t saved_module_state_id;
    uint64_t roots_epoch;
} ModuleDescriptor;

// Initialize the registry owned by one Runtime.  Module namespace Items are
// rooted for this runtime only; they must never be shared across runtimes.
void module_registry_init_for_runtime(Runtime* runtime);

// Release the registry owned by one Runtime before its heap is destroyed.
void module_registry_cleanup_for_runtime(Runtime* runtime);

// Compatibility lifecycle hooks resolve the Runtime from the active
// EvalContext.  New host code should use the explicit Runtime variants.
void module_registry_init(void);
void module_registry_cleanup(void);

// Ordered descriptor traversal is used by JS top-level-await scheduling. The
// registry owns the list, so language runtimes do not maintain a second cache.
ModuleDescriptor* module_registry_first_for_runtime(Runtime* runtime);
ModuleDescriptor* module_registry_next(ModuleDescriptor* module);

// Register a compiled + executed module
void module_register(const char* path, const char* lang, Item namespace_obj, void* mir_ctx);
void module_register_for_runtime(Runtime* runtime, const char* path, const char* lang,
                                 Item namespace_obj, void* mir_ctx);
void module_register_with_namespace_ops(const char* path, const char* lang,
                                        Item namespace_obj, void* mir_ctx,
                                        const ModuleNamespaceOps* namespace_ops);

void module_register_with_namespace_ops_for_runtime(
    Runtime* runtime, const char* path, const char* lang, Item namespace_obj,
    void* mir_ctx, const ModuleNamespaceOps* namespace_ops);

// Look up a module by resolved path. Returns NULL if not found.
ModuleDescriptor* module_get(const char* path);
ModuleDescriptor* module_get_for_runtime(Runtime* runtime, const char* path);

// Check if a module is already loaded
bool module_is_loaded(const char* path);

// Mark a module as currently loading (for circular import detection).
// Creates a partial entry with loading=true, initialized=false, empty namespace.
// Returns the descriptor so the caller can set namespace_obj after execution.
ModuleDescriptor* module_register_loading(const char* path, const char* lang);
ModuleDescriptor* module_register_loading_with_namespace_ops(
    const char* path, const char* lang, const ModuleNamespaceOps* namespace_ops);
ModuleDescriptor* module_register_loading_with_namespace_ops_for_runtime(
    Runtime* runtime, const char* path, const char* lang,
    const ModuleNamespaceOps* namespace_ops);

// Check if a module is currently being loaded (circular import in progress)
bool module_is_loading(const char* path);

// Build a namespace Item (map) from a compiled Lambda module's public API.
// Walks the AST for pub functions and variables, wrapping compiled function
// pointers as Function Items and reading variable values from the module struct.
// Returns the namespace Item (type=MAP).
Item module_build_lambda_namespace(void* script_ptr);

// Create a synthetic Script from a hosted namespace object for Lambda imports.
// Walks the language-owned namespace map's shape entries and creates synthetic AST nodes
// (AstFuncNode/AstNamedNode) so the Lambda import system can bind them.
// The returned Script has ast_root, reference, and is registered in runtime->scripts.
// Returns NULL on error.
void* create_module_import_script(const char* resolved_path, Item namespace_obj, void* runtime_ptr);

Item module_namespace_get(const ModuleDescriptor* module, const char* name);
int module_namespace_function_arity(const ModuleDescriptor* module, Item function_obj);
void* module_namespace_function_ptr(const ModuleDescriptor* module, Item function_obj);

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
