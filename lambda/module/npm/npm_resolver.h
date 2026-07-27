#ifndef NPM_RESOLVER_H
#define NPM_RESOLVER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Resolved package in the dependency tree
// ---------------------------------------------------------------------------

typedef struct {
    char* name;
    char* version;
    char* tarball_url;
    char* integrity;
    // dependencies (name → resolved version)
    char** dep_names;
    char** dep_versions;
    int    dep_count;
} NpmResolvedPackage;

// ---------------------------------------------------------------------------
// Resolution result: flat list of all packages to install
// ---------------------------------------------------------------------------

typedef struct {
    NpmResolvedPackage* packages;
    int                 count;
    int                 cap;
    bool                success;
    char*               error;  // error message if !success
} NpmResolutionResult;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Resolve a dependency tree from a list of top-level dependencies.
// Each dependency is name + range_str.
// Uses lockfile for already-resolved versions when available.
// Fetches metadata from the npm registry for unresolved packages.
NpmResolutionResult* npm_resolve_dependencies(
    const char** dep_names,
    const char** dep_ranges,
    int dep_count,
    const char* lockfile_path   // optional, may be NULL
);

// Free a resolution result.
void npm_resolution_free(NpmResolutionResult* result);

#ifdef __cplusplus
}
#endif

#endif // NPM_RESOLVER_H
