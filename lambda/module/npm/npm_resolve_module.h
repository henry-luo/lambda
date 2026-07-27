#ifndef NPM_RESOLVE_MODULE_H
#define NPM_RESOLVE_MODULE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Module resolution result
// ---------------------------------------------------------------------------

typedef struct {
    char* resolved_path;    // absolute file path to the resolved module
    char* package_dir;      // directory of the package containing the module
    bool  is_esm;           // true if module should be treated as ESM
    bool  found;
} NpmModuleResolution;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Resolve a module specifier from a given directory.
// Implements the Node.js module resolution algorithm:
//   1. Check if specifier is a builtin (node:fs, etc.) → return NULL (handled elsewhere)
//   2. If starts with './' or '../' or '/' → resolve as relative file
//   3. Otherwise → walk up node_modules directories
//
// For each candidate:
//   a. Try exact file
//   b. Try with .js, .mjs, .cjs, .json extensions
//   c. Try as directory (look for package.json "exports"/"main", then index.js)
//
// conditions: null-terminated array (e.g. ["lambda", "node", "import", "default"])
NpmModuleResolution npm_resolve_module(const char* specifier,
                                       const char* from_dir,
                                       const char** conditions,
                                       int condition_count);

// Free the resolution result.
void npm_module_resolution_free(NpmModuleResolution* res);

#ifdef __cplusplus
}
#endif

#endif // NPM_RESOLVE_MODULE_H
