#ifndef NPM_INSTALLER_H
#define NPM_INSTALLER_H

#include <stdbool.h>
#include "npm_resolver.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Install options
// ---------------------------------------------------------------------------

typedef struct {
    bool production;      // skip devDependencies
    bool dry_run;         // print what would be done without doing it
    bool verbose;         // print verbose output
} NpmInstallOptions;

// ---------------------------------------------------------------------------
// Install result
// ---------------------------------------------------------------------------

typedef struct {
    int packages_installed;
    int packages_cached;     // already in cache, skipped download
    bool success;
    char* error;
} NpmInstallResult;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Install all dependencies from package.json in the current directory.
// Creates node_modules/ with pnpm-style flat + symlink layout:
//   node_modules/.lambda/<name>@<version>/node_modules/<name>/  (real files)
//   node_modules/<name> → .lambda/<name>@<version>/node_modules/<name>  (symlink)
NpmInstallResult* npm_install(const char* project_dir, const NpmInstallOptions* opts);

// Install a specific package (like `npm install lodash`).
NpmInstallResult* npm_install_package(const char* project_dir, const char* package_name,
                                      const char* version_range, bool is_dev,
                                      const NpmInstallOptions* opts);

// Uninstall a package (remove from package.json and node_modules).
int npm_uninstall(const char* project_dir, const char* package_name);

// Free install result.
void npm_install_result_free(NpmInstallResult* result);

#ifdef __cplusplus
}
#endif

#endif // NPM_INSTALLER_H
