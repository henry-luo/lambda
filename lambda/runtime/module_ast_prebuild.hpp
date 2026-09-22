#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../lib/arraylist.h"

// The module prebuild graph is deliberately language-neutral. A profile owns
// syntax discovery, resolution, and the isolated AST build; this scheduler
// owns one bounded closure-wide queue plus dependency continuations.
typedef enum ModuleAstLanguage {
    MODULE_AST_LANGUAGE_LAMBDA = 0,
    MODULE_AST_LANGUAGE_JAVASCRIPT,
    MODULE_AST_LANGUAGE_COUNT,
} ModuleAstLanguage;

typedef struct ModuleAstResolvedImport {
    char* path;  // owned by the graph after a successful resolution
    ModuleAstLanguage language;
} ModuleAstResolvedImport;

// Returns an ArrayList of owned char* specifiers. NULL means that discovery
// failed or found no static imports; normal loading remains authoritative.
typedef ArrayList* (*ModuleAstDiscoverImportsFn)(void* context,
    const char* source, size_t source_length);
typedef bool (*ModuleAstResolveImportFn)(void* context,
    const char* importer_path, const char* specifier,
    ModuleAstResolvedImport* out);
// Called only from a prebuild worker. It must publish an AST cache artifact
// and must never initialize a module or lower MIR.
typedef bool (*ModuleAstBuildFn)(void* context, const char* path);

typedef struct ModuleAstPrebuildProfile {
    const char* name;
    ModuleAstLanguage language;
    ModuleAstDiscoverImportsFn discover_imports;
    ModuleAstResolveImportFn resolve_import;
    ModuleAstBuildFn build_ast;
    void* context;
} ModuleAstPrebuildProfile;

typedef struct ModuleAstPrebuildProfiles {
    const ModuleAstPrebuildProfile* profiles[MODULE_AST_LANGUAGE_COUNT];
} ModuleAstPrebuildProfiles;

typedef struct ModuleAstPrebuildStats {
    uint32_t discovered_modules;
    uint32_t built_modules;
    uint32_t cache_ready_modules;
    uint32_t failed_modules;
    uint32_t worker_pool_runs;
} ModuleAstPrebuildStats;

// Build a static import closure through one dependency-aware queue. Discovery
// jobs publish nested requests immediately; a module build waits only on its
// direct prerequisites, never on a depth batch. This is best-effort: a missing
// or invalid module is left for the ordinary loader's established diagnostics.
bool module_ast_prebuild_imports(const ModuleAstPrebuildProfiles* profiles,
    ModuleAstLanguage root_language, const char* root_path,
    const char* root_source, size_t root_source_length,
    ModuleAstPrebuildStats* out_stats);
