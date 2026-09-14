#pragma once
#ifndef LAMBDA_INPUT_SCRIPT_CACHE_H
#define LAMBDA_INPUT_SCRIPT_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
class InputManager;
extern "C" {
#else
typedef struct InputManager InputManager;
#endif

typedef struct InputScriptCache InputScriptCache;
typedef struct InputCacheScope InputCacheScope;
typedef struct InputScriptLease InputScriptLease;
typedef struct ScriptInput ScriptInput;

typedef enum InputScriptSourceKind {
    INPUT_SCRIPT_SOURCE_FILE = 1,
    INPUT_SCRIPT_SOURCE_URL,
    INPUT_SCRIPT_SOURCE_INLINE,
    INPUT_SCRIPT_SOURCE_GENERATED,
    INPUT_SCRIPT_SOURCE_HARNESS,
} InputScriptSourceKind;

typedef enum InputScriptCachePolicy {
    INPUT_SCRIPT_CACHE_OFF = 0,
    INPUT_SCRIPT_CACHE_AST,
    INPUT_SCRIPT_CACHE_MIR,
    INPUT_SCRIPT_CACHE_ALL,
} InputScriptCachePolicy;

// the request separates source identity from compiler policy. The cache copies
// all referenced bytes/strings before returning, so callers may release their
// temporary source and path buffers after acquisition.
typedef struct InputScriptRequest {
    const char* identity;          // canonical path/URL or explicit snapshot id
    const char* source;
    size_t source_length;
    InputScriptSourceKind source_kind;
    const char* language;
    const char* profile;
    const char* parser_abi;
    const char* parse_flags;
    const char* resolution_base;
    const char* backend;
    const char* execution_mode;
    uint64_t ast_abi;
    uint64_t compiler_abi;
    uint64_t interface_abi;
    uint64_t dependency_digest;
    uint32_t optimize_level;
    bool module_mode;
} InputScriptRequest;

typedef struct InputScriptArtifactOps {
    void (*destroy)(void* artifact);
    size_t (*retained_bytes)(const void* artifact);
} InputScriptArtifactOps;

typedef enum InputScriptBuildKind {
    INPUT_SCRIPT_BUILD_AST = 1,
    INPUT_SCRIPT_BUILD_MIR,
} InputScriptBuildKind;

typedef enum InputScriptBuildClaim {
    INPUT_SCRIPT_BUILD_BYPASS = 0,
    INPUT_SCRIPT_BUILD_OWNER,
    INPUT_SCRIPT_BUILD_READY,
    INPUT_SCRIPT_BUILD_POISONED,
} InputScriptBuildClaim;

typedef struct InputScriptCacheStats {
    uint64_t source_lookups;
    uint64_t source_hits;
    uint64_t source_misses;
    uint64_t ast_lookups;
    uint64_t ast_hits;
    uint64_t ast_misses;
    uint64_t ast_builds;
    uint64_t mir_lookups;
    uint64_t mir_hits;
    uint64_t mir_misses;
    uint64_t mir_builds;
    uint64_t module_hits;
    uint64_t invalidations;
    uint64_t dependency_invalidations;
    uint64_t rejected;
    uint64_t poisoned;
    uint64_t evictions;
    uint64_t single_flight_waits;
    uint64_t retained_source_bytes;
    uint64_t retained_ast_bytes;
    uint64_t retained_mir_bytes;
    uint64_t peak_bytes;
    uint64_t retention_limit_bytes;
    uint64_t retention_pressure;
    uint64_t scopes_opened;
    uint64_t leases_acquired;
    uint64_t leases_released;
    uint64_t retained_entries;
} InputScriptCacheStats;

InputScriptCache* input_script_cache_create(void);
void input_script_cache_destroy(InputScriptCache* cache);
InputScriptCachePolicy input_script_cache_policy(const InputScriptCache* cache);
bool input_script_cache_ast_enabled(const InputScriptCache* cache);
bool input_script_cache_mir_enabled(const InputScriptCache* cache);

// inputmanager owns the returned cache for its entire process lifetime.
InputScriptCache* input_manager_script_cache(InputManager* manager);
InputScriptCache* input_manager_global_script_cache(void);
InputCacheScope* input_manager_open_script_scope(
    InputManager* manager, const InputScriptRequest* request);
void input_manager_close_script_scope(InputCacheScope* scope);

InputCacheScope* input_script_cache_open_scope(InputScriptCache* cache);
void input_script_cache_close_scope(InputCacheScope* scope);
InputScriptLease* input_script_cache_acquire(
    InputCacheScope* scope, const InputScriptRequest* request);
InputScriptLease* input_script_cache_acquire_file(
    InputCacheScope* scope, const InputScriptRequest* request,
    const char* path);
// copy an exact supplied source snapshot while keeping cache ownership
// internal to the call.
char* input_script_cache_copy_source(
    InputScriptCache* cache, const InputScriptRequest* request,
    const char* source, size_t source_length, size_t* out_length);
// copy an exact file snapshot while keeping cache ownership internal to the call.
char* input_script_cache_copy_file_source(
    InputScriptCache* cache, const InputScriptRequest* request,
    const char* path, size_t* out_length);
void input_script_cache_release(InputScriptLease* lease);
// retire matching source generations without invalidating active leases.
size_t input_script_cache_invalidate(
    InputScriptCache* cache, const InputScriptRequest* request);
// Retire one logical source generation and every cached importer that depends
// on it. Callers use this when a cached artifact loses its freshness proof
// without a source-byte change at the root.
size_t input_script_cache_invalidate_unit(InputScriptCache* cache,
    uint32_t compilation_unit_id);

ScriptInput* input_script_lease_input(InputScriptLease* lease);
const char* input_script_source(const ScriptInput* input);
size_t input_script_source_length(const ScriptInput* input);
const char* input_script_identity(const ScriptInput* input);
uint32_t input_script_compilation_unit_id(const ScriptInput* input);

// Records a source-level dependency between two persistent compilation units.
// A later source-generation invalidation retires the complete importer cone;
// active execution leases still delay artifact destruction.
bool input_script_cache_record_dependency_by_unit(InputScriptCache* cache,
    uint32_t importer_unit_id, uint32_t dependency_unit_id);
// Re-read a file-backed logical unit when an adapter's cheap file detector
// changed. Exact bytes decide whether its importer cone must be retired.
// Returns false only when the unit cannot be inspected; out_changed reports
// a successful source-generation retirement.
bool input_script_cache_refresh_file_unit(InputScriptCache* cache,
    uint32_t compilation_unit_id, const char* path, bool* out_changed);
// Refresh every file-backed dependency reachable from one logical unit. A
// failed freshness proof retires that unit's importer cone before a caller can
// reuse its compiled image (D8.5.1v3).
bool input_script_cache_refresh_file_dependencies(InputScriptCache* cache,
    uint32_t compilation_unit_id, bool* out_changed);

bool input_script_cache_get_ast(InputScriptLease* lease, void** out_ast);
bool input_script_cache_publish_ast(InputScriptLease* lease, void* ast,
    const InputScriptArtifactOps* ops);
bool input_script_cache_get_mir(InputScriptLease* lease, void** out_mir);
bool input_script_cache_publish_mir(InputScriptLease* lease, void* mir,
    const InputScriptArtifactOps* ops);

// Claim one artifact build for this exact source/compiler key. A READY result
// means another worker already published it; an OWNER must always complete its
// claim. POISONED is fail-closed until a source-generation invalidation.
InputScriptBuildClaim input_script_cache_claim_build(InputScriptLease* lease,
    InputScriptBuildKind kind);
void input_script_cache_complete_build(InputScriptLease* lease,
    InputScriptBuildKind kind, bool published, bool poison);

void input_script_cache_mark_module_hit(InputScriptCache* cache);
void input_script_cache_mark_dependency_invalidation(InputScriptCache* cache);
void input_script_cache_mark_rejected(InputScriptCache* cache);
void input_script_cache_mark_poisoned(InputScriptCache* cache);
void input_script_cache_get_stats(InputScriptCache* cache,
    InputScriptCacheStats* out_stats);
void input_script_cache_log_summary(InputScriptCache* cache);

#ifdef __cplusplus
}
#endif

#endif
