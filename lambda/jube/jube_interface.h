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

// Removes records compiled for one module while its registration transaction
// rolls back. The module descriptor must still be live while this runs.
void jube_interface_remove_module(const JubeModuleDef* module);

// True when the typedef has a compiled interface record set.
bool jube_type_has_interface(const JubeTypeDef* type);

// Record-driven dispatch. Each returns 1 when the receiver's type has a
// compiled interface and the operation was handled (out is set), 0 when the
// receiver is not a declared host object.
int jube_member_get(Item receiver, Item key, Item* out);
int jube_member_projected_get(Item receiver, Item key, Item* out);
int jube_member_set(Item receiver, Item key, Item value, Item* out);
int jube_member_has(Item receiver, Item key, Item* out);
int jube_member_delete(Item receiver, Item key, Item* out);
int jube_member_descriptor(Item receiver, Item key, Item* out);
int jube_member_own_keys(Item receiver, Item* out);
int jube_member_projection_keys(Item receiver, Item* out);
int jube_member_prototype(Item receiver, Item* out);
void* jube_host_identity(Item item);

// DOM4 compile-time registry queries. Slots are process-stable registry
// identities; ordinals are declaration-order member positions.
enum JubeMemberKindValue {
    JUBE_MEMBER_KIND_FIELD = 0,
    JUBE_MEMBER_KIND_METHOD = 1,
    JUBE_MEMBER_KIND_CONST = 2,
};
const JubeTypeDef* jube_iface_type_by_name(const char* name, uint32_t len);
int jube_iface_type_slot(const JubeTypeDef* type);
int jube_member_count(const JubeTypeDef* type);
const char* jube_member_name_at(const JubeTypeDef* type, int ordinal, bool camel_case);
int jube_member_ordinal(const JubeTypeDef* type, const char* name, uint32_t len);
uint8_t jube_member_kind_at(const JubeTypeDef* type, int ordinal);
bool jube_member_can_raise_at(const JubeTypeDef* type, int ordinal);
int jube_member_arity_at(const JubeTypeDef* type, int ordinal);
const JubeTypeDef* jube_member_result_type_at(const JubeTypeDef* type, int ordinal);

// Digest of the activated declaration/binding registry. MIR cache keys fold
// this in because baked member slots and ordinals are binary-local identities.
uint64_t jube_interface_registry_digest(void);

// DOM4 guarded by-ordinal runtime entries. A zero return means that the
// receiver brand/ordinal guard did not match and the caller must use generic
// property semantics.
int jube_member_get_by_ordinal(Item receiver, int slot, uint32_t ordinal, Item* out);
int jube_member_set_by_ordinal(Item receiver, int slot, uint32_t ordinal,
                               Item value, Item* out);
int jube_member_call_by_ordinal(Item receiver, int slot, uint32_t ordinal,
                                Item* args, int argc, Item* out);

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
