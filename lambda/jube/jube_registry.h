#pragma once

#include "jube.h"

#ifdef __cplusplus
extern "C" {
#endif

int jube_register_static_module(const JubeModuleDef* module);
int jube_load_dynamic_module(const char* path, const char* entry_symbol);
// Supplies argv[0] so the registry can discover a bundle next to the unified
// host even when the current working directory is a user project.
void jube_set_host_executable_path(const char* executable_path);
// Resolve and load one manifest-selected hosted language on a CLI/import
// fallback. It performs no work for already registered languages.
bool jube_discover_hosted_language(const char* selector);
void jube_register_builtin_modules(void);
int jube_static_module_count(void);
const JubeModuleDef* jube_static_module_at(int index);
const JubeModuleDef* jube_find_static_module(const char* name);
const JubeLanguageDef* jube_module_language(const JubeModuleDef* module);
void jube_notify_heap_cleanup(void* heap);
// Releases process-lifetime Jube registry allocations before memtrack shutdown.
void jube_registry_cleanup(void);
const JubeTypeDef* jube_find_type_by_host_type(const void* host_type);
void jube_modules_runtime_reset(void);

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
