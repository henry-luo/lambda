#ifndef NPM_REGISTRY_H
#define NPM_REGISTRY_H

#include <stdbool.h>
#include "semver.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Registry metadata for a package version
// ---------------------------------------------------------------------------

typedef struct {
    char* name;             // package name
    char* version;          // resolved version
    char* tarball_url;      // download URL for .tgz
    char* integrity;        // SHA integrity hash (e.g. "sha512-...")
    // dependencies as name=range pairs
    char** dep_names;
    char** dep_ranges;
    int    dep_count;
} NpmRegistryVersion;

// ---------------------------------------------------------------------------
// Registry metadata for an entire package (all versions)
// ---------------------------------------------------------------------------

typedef struct {
    char*  name;
    char** version_strings;  // all available version strings
    int    version_count;
    // dist-tags
    char*  latest;          // "latest" dist-tag version
} NpmRegistryPackage;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Fetch full metadata for a package from the registry.
// Returns NULL on error. Caller must free with npm_registry_package_free().
NpmRegistryPackage* npm_registry_fetch_package(const char* package_name);

// Fetch metadata for a specific version that satisfies a semver range.
// Returns NULL if no version satisfies. Caller must free with npm_registry_version_free().
NpmRegistryVersion* npm_registry_resolve_version(const char* package_name, const char* range_str);

// Download a tarball to the global cache. Returns path to cached .tgz, or NULL.
// Caller must free() the returned path.
char* npm_registry_download_tarball(const char* tarball_url, const char* integrity);

// Free functions
void npm_registry_package_free(NpmRegistryPackage* pkg);
void npm_registry_version_free(NpmRegistryVersion* ver);

#ifdef __cplusplus
}
#endif

#endif // NPM_REGISTRY_H
