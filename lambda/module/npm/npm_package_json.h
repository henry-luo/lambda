#ifndef NPM_PACKAGE_JSON_H
#define NPM_PACKAGE_JSON_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Parsed dependency entry
// ---------------------------------------------------------------------------

typedef struct {
    const char* name;       // package name (points into parsed JSON data)
    const char* range;      // version range string
} NpmDependency;

// ---------------------------------------------------------------------------
// Parsed package.json representation
// ---------------------------------------------------------------------------

typedef struct {
    // identity
    const char* name;       // package name
    const char* version;    // package version string

    // entry points
    const char* main;       // CJS entry point ("main" field)
    const char* module;     // ESM entry point ("module" field)
    const char* type;       // "module" or "commonjs" (default: "commonjs")

    // exports map (stored as raw Item for flexible traversal)
    // Could be a string or a nested map with conditions
    void* exports_item;     // Item (cast from void* for C compat)
    bool  has_exports;

    // imports map (self-referencing)
    void* imports_item;
    bool  has_imports;

    // dependencies
    NpmDependency* dependencies;
    int             dep_count;

    NpmDependency* dev_dependencies;
    int             dev_dep_count;

    NpmDependency* peer_dependencies;
    int             peer_dep_count;

    // scripts
    NpmDependency* scripts;  // reuse struct: name=script_name, range=command
    int             script_count;

    // bin
    NpmDependency* bin;      // reuse struct: name=bin_name, range=file_path
    int             bin_count;

    // metadata
    const char* description;
    const char* license;

    // raw parsed data (for accessing fields not explicitly extracted)
    void* raw_item;         // the raw Item from JSON parse

    // validity
    bool valid;
} NpmPackageJson;

// Parse a package.json file at the given path.
// Returns a heap-allocated NpmPackageJson. Caller must free with npm_package_json_free().
NpmPackageJson* npm_package_json_parse(const char* file_path);

// Parse package.json from a string.
NpmPackageJson* npm_package_json_parse_string(const char* json_content);

// Free a parsed package.json.
void npm_package_json_free(NpmPackageJson* pkg);

// Resolve the entry point for a package given an import specifier and conditions.
// conditions: null-terminated array of condition strings like ["lambda", "node", "import", "default"]
// Returns the resolved file path relative to the package root, or NULL.
const char* npm_resolve_exports(NpmPackageJson* pkg, const char* subpath,
                                const char** conditions, int condition_count);

#ifdef __cplusplus
}
#endif

#endif // NPM_PACKAGE_JSON_H
