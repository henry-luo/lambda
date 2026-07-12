#pragma once

// DOM3 interface compiler + record-driven host-object dispatch.
// Engine-internal: modules see only jube.h (interface_decl + binding tables);
// the compiled member records and dispatch entry points below are consumed by
// the generic host-object paths in js_runtime.cpp / js_globals.cpp / vmap.cpp.

#include "jube.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Registration-time compile of a module's interface_decl against its binding
// tables. Returns 0 on success (or when the module declares no interface),
// -1 on parse/cross-check failure — registration must then fail.
int jube_compile_module_interface(const JubeModuleDef* module);

// True when the typedef has a compiled interface record set.
bool jube_type_has_interface(const JubeTypeDef* type);

// Record-driven dispatch. Each returns 1 when the receiver's type has a
// compiled interface and the operation was handled (out is set), 0 to fall
// through to the type's host_ops / legacy paths.
int jube_member_get(Item receiver, Item key, Item* out);
int jube_member_projected_get(Item receiver, Item key, Item* out);
int jube_member_set(Item receiver, Item key, Item value, Item* out);
int jube_member_call(Item receiver, Item name, Item* args, int argc, Item* out);
int jube_member_has(Item receiver, Item key, Item* out);
int jube_member_delete(Item receiver, Item key, Item* out);
int jube_member_descriptor(Item receiver, Item key, Item* out);
int jube_member_own_keys(Item receiver, Item* out);
int jube_member_prototype(Item receiver, Item* out);
void* jube_host_identity(Item item);

// Engine host-API table, for internal consumers of the same services modules
// receive at init (function objects, GC roots, value construction).
const JubeHostAPI* jube_internal_host_api(void);

// Frees compiled records at process exit (before the memtrack leak report).
void jube_interface_cleanup(void);

// Drops cached prototypes/method Items when a JS runtime resets (batch mode
// recreates globals per script; seeds must rebuild against the new runtime).
void jube_interface_runtime_reset(void);

// Size-gated accessors for the DOM3 additive tail of JubeModuleDef.
const char* jube_module_interface_decl(const JubeModuleDef* module);
const JubeTypeBinding* jube_module_type_bindings(const JubeModuleDef* module,
                                                 int32_t* count);

#ifdef __cplusplus
}
#endif
