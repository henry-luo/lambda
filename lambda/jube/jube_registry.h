#pragma once

#include "jube.h"

#ifdef __cplusplus
extern "C" {
#endif

int jube_register_static_module(const JubeModuleDef* module);
int jube_load_dynamic_module(const char* path, const char* entry_symbol);

// Runtime-library specifiers are cataloged separately from dynamic activation.
// Catalog queries never execute a module initializer; resolve may activate a
// manifest-selected module only when a script actually requests its namespace.
typedef enum JubeSpecifierResolveStatus {
    JUBE_SPECIFIER_UNKNOWN = 0,
    JUBE_SPECIFIER_UNAVAILABLE = 1,
    JUBE_SPECIFIER_ACTIVATION_FAILED = 2,
    JUBE_SPECIFIER_RESOLVED = 3,
} JubeSpecifierResolveStatus;

typedef bool (*JubeSpecifierNameCallback)(const char* name, void* user);

bool jube_specifier_catalog_contains(const char* name);
bool jube_specifier_is_builtin(const char* name);
// Resolves only a descriptor already resident in this process, so it never
// loads a library. Requesting its namespace may initialize and attach it.
JubeSpecifierResolveStatus jube_specifier_resolve_active(const char* name, Item* out_namespace);
JubeSpecifierResolveStatus jube_specifier_resolve(const char* name, Item* out_namespace);
bool jube_specifier_index_names(JubeSpecifierNameCallback callback, void* user);
// Supplies argv[0] so the registry can discover a bundle next to the unified
// host even when the current working directory is a user project.
void jube_set_host_executable_path(const char* executable_path);
// Resolve and load one manifest-selected hosted language on a CLI/import
// fallback. It performs no work for already registered languages.
bool jube_discover_hosted_language(const char* selector);
void jube_register_builtin_modules(void);
// Returns whether the selected bundle profile includes the optional Node
// compatibility descriptor. The executable owns its registration because
// validation DSOs deliberately do not link node-core implementation objects.
bool jube_node_core_module_enabled(void);
// Tests and launchers select Node compatibility services through the same
// module-set manifest. Optional static modules must not bypass that profile.
bool jube_node_module_enabled(const char* module_name);
// The registry owns generation-checked resource ids for hosted Node services.
// With no registered resources, process inventory reports an empty array.
uint32_t jube_node_resource_add(Item value, const char* kind);
void jube_node_resource_remove(uint32_t resource_id);
void jube_node_resource_clear(void);
bool jube_node_resource_contains(uint32_t resource_id);
Item jube_node_resource_active_handles(void);
Item jube_node_resource_active_resources_info(void);
int jube_static_module_count(void);
const JubeModuleDef* jube_static_module_at(int index);
const JubeModuleDef* jube_find_static_module(const char* name);
// Registration catalogs a descriptor without running guest callbacks. Actual
// imports, namespaces, globals, languages, and host-type use activate it here.
bool jube_activate_module(const JubeModuleDef* module);
bool jube_resolve_global(const char* name, size_t name_length, Item* out_value);
const JubeGlobalDef* jube_module_globals(const JubeModuleDef* module, int32_t* count);
const JubeLanguageDef* jube_module_language(const JubeModuleDef* module);
void jube_notify_heap_cleanup(void* heap);
// Releases process-lifetime Jube registry allocations before memtrack shutdown.
void jube_registry_cleanup(void);
const JubeTypeDef* jube_find_type_by_host_type(const void* host_type);
void jube_modules_runtime_reset(void);
// JS owns the lifecycle boundary and calls these only while its current heap
// is live. The opaque session becomes invalid immediately after detach.
void jube_modules_runtime_attach(void);
void jube_modules_runtime_detach(void);

typedef enum JubeNodeModuleStateSlot {
    JUBE_NODE_MODULE_STATE_EVENTS = 0,
    JUBE_NODE_MODULE_STATE_STRING_DECODER,
    JUBE_NODE_MODULE_STATE_URL,
    JUBE_NODE_MODULE_STATE_TTY,
    JUBE_NODE_MODULE_STATE_PROCESS,
    JUBE_NODE_MODULE_STATE_CONSTANTS,
    JUBE_NODE_MODULE_STATE_TIMERS,
    JUBE_NODE_MODULE_STATE_PUNYCODE,
    JUBE_NODE_MODULE_STATE_WORKERS,
    JUBE_NODE_MODULE_STATE_QUERYSTRING,
    JUBE_NODE_MODULE_STATE_V8,
    JUBE_NODE_MODULE_STATE_OS,
    JUBE_NODE_MODULE_STATE_PERF_HOOKS,
    JUBE_NODE_MODULE_STATE_PATH,
    JUBE_NODE_MODULE_STATE_CORE,
} JubeNodeModuleStateSlot;

// Node compatibility modules keep private native records in fixed slots on
// the current EvalContext-owned Jube session. Slot acquisition is a cold
// runtime_attach operation; normal module calls only load the chosen slot.
void* jube_node_session_module_state_get(void* session, uint32_t slot, size_t size);
void* jube_node_current_module_state(uint32_t slot);

// Internal host bridge for import-time language dispatch.  The returned
// wrapper is opaque to the language module and is always released by the
// language registry unless its activation was retained for heap cleanup.
void* jube_create_import_execution(void* host_context);
void jube_destroy_import_execution(void* execution_context);
bool jube_import_execution_is_retained(void* execution_context);
void* jube_execution_runtime_handle(void* execution_context);

// DOM3: shared per-type prototype object (lazy, GC-rooted) for types with a
// compiled interface declaration; modules attach constructors to it so
// instanceof sees one prototype identity across engine and module.
Item jube_type_prototype(const JubeTypeDef* type);

#ifdef __cplusplus
}
#endif
