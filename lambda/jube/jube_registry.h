#pragma once

#include "jube.h"

#ifdef __cplusplus
extern "C" {
#endif

int jube_register_static_module(const JubeModuleDef* module);
int jube_load_dynamic_module(const char* path, const char* entry_symbol);
void jube_register_builtin_modules(void);
int jube_static_module_count(void);
const JubeModuleDef* jube_static_module_at(int index);
const JubeModuleDef* jube_find_static_module(const char* name);
void jube_notify_heap_cleanup(void* heap);
const JubeTypeDef* jube_find_type_by_host_type(const void* host_type);
void jube_modules_runtime_reset(void);

// DOM3: shared per-type prototype object (lazy, GC-rooted) for types with a
// compiled interface declaration; modules attach constructors to it so
// instanceof sees one prototype identity across engine and module.
Item jube_type_prototype(const JubeTypeDef* type);

#ifdef __cplusplus
}
#endif
