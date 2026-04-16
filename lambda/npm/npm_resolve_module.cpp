// npm_resolve_module.cpp — Node.js module resolution algorithm

#include "npm_resolve_module.h"
#include "npm_package_json.h"
#include "../lib/file.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Extension resolution
// ---------------------------------------------------------------------------

static const char* extensions[] = { "", ".js", ".mjs", ".cjs", ".json", NULL };

// try to resolve a path with various extensions
static char* try_file_extensions(const char* base_path) {
    for (int i = 0; extensions[i]; i++) {
        char path[2048];
        snprintf(path, sizeof(path), "%s%s", base_path, extensions[i]);
        if (file_is_file(path)) {
            return mem_strdup(path, MEM_CAT_JS_RUNTIME);
        }
    }
    return NULL;
}

// try to resolve as directory: check package.json, then index.js
static char* try_directory(const char* dir_path,
                           const char** conditions, int condition_count,
                           bool* out_esm) {
    // check package.json
    char pkg_path[2048];
    snprintf(pkg_path, sizeof(pkg_path), "%s/package.json", dir_path);

    if (file_is_file(pkg_path)) {
        NpmPackageJson* pkg = npm_package_json_parse(pkg_path);
        if (pkg) {
            // try "exports" first (modern)
            if (pkg->has_exports) {
                const char* resolved = npm_resolve_exports(pkg, ".", conditions, condition_count);
                if (resolved) {
                    char full[2048];
                    snprintf(full, sizeof(full), "%s/%s", dir_path, resolved);
                    if (file_is_file(full)) {
                        if (out_esm) *out_esm = (pkg->type && strcmp(pkg->type, "module") == 0);
                        npm_package_json_free(pkg);
                        return mem_strdup(full, MEM_CAT_JS_RUNTIME);
                    }
                }
            }

            // try "module" field (ESM entry)
            if (pkg->module) {
                char full[2048];
                snprintf(full, sizeof(full), "%s/%s", dir_path, pkg->module);
                if (file_is_file(full)) {
                    if (out_esm) *out_esm = true;
                    npm_package_json_free(pkg);
                    return mem_strdup(full, MEM_CAT_JS_RUNTIME);
                }
            }

            // try "main" field (CJS entry)
            if (pkg->main) {
                char full[2048];
                snprintf(full, sizeof(full), "%s/%s", dir_path, pkg->main);
                // try with extensions
                char* resolved = try_file_extensions(full);
                if (resolved) {
                    if (out_esm) *out_esm = (pkg->type && strcmp(pkg->type, "module") == 0);
                    npm_package_json_free(pkg);
                    return resolved;
                }
                // try as directory
                if (file_is_dir(full)) {
                    char idx[2048];
                    snprintf(idx, sizeof(idx), "%s/index.js", full);
                    if (file_is_file(idx)) {
                        if (out_esm) *out_esm = (pkg->type && strcmp(pkg->type, "module") == 0);
                        npm_package_json_free(pkg);
                        return mem_strdup(idx, MEM_CAT_JS_RUNTIME);
                    }
                }
            }

            if (out_esm) *out_esm = (pkg->type && strcmp(pkg->type, "module") == 0);
            npm_package_json_free(pkg);
        }
    }

    // fallback: index.js, index.mjs, index.cjs, index.json
    const char* index_files[] = { "index.js", "index.mjs", "index.cjs", "index.json", NULL };
    for (int i = 0; index_files[i]; i++) {
        char idx_path[2048];
        snprintf(idx_path, sizeof(idx_path), "%s/%s", dir_path, index_files[i]);
        if (file_is_file(idx_path)) {
            return mem_strdup(idx_path, MEM_CAT_JS_RUNTIME);
        }
    }

    return NULL;
}

// determine if a file is ESM based on extension and nearest package.json
static bool is_esm_file(const char* file_path) {
    const char* ext = file_path_ext(file_path);
    if (ext) {
        if (strcmp(ext, ".mjs") == 0) return true;
        if (strcmp(ext, ".cjs") == 0) return false;
    }
    // check nearest package.json for "type" field
    // walk up directories
    char dir[2048];
    char* dirname = file_path_dirname(file_path);
    if (!dirname) return false;
    strncpy(dir, dirname, sizeof(dir) - 1);
    mem_free(dirname);

    while (dir[0]) {
        char pkg_path[2048];
        snprintf(pkg_path, sizeof(pkg_path), "%s/package.json", dir);
        if (file_is_file(pkg_path)) {
            NpmPackageJson* pkg = npm_package_json_parse(pkg_path);
            if (pkg) {
                bool esm = (pkg->type && strcmp(pkg->type, "module") == 0);
                npm_package_json_free(pkg);
                return esm;
            }
        }
        // go up one directory
        char* parent = file_path_dirname(dir);
        if (!parent || strcmp(parent, dir) == 0) {
            mem_free(parent);
            break;
        }
        strncpy(dir, parent, sizeof(dir) - 1);
        mem_free(parent);
    }
    return false; // default: CJS
}

// ---------------------------------------------------------------------------
// Subpath exports resolution
// ---------------------------------------------------------------------------

static char* resolve_package_subpath(const char* package_dir, const char* subpath,
                                     const char** conditions, int condition_count,
                                     bool* out_esm) {
    char pkg_path[2048];
    snprintf(pkg_path, sizeof(pkg_path), "%s/package.json", package_dir);

    if (!file_is_file(pkg_path)) return NULL;

    NpmPackageJson* pkg = npm_package_json_parse(pkg_path);
    if (!pkg) return NULL;

    if (out_esm) *out_esm = (pkg->type && strcmp(pkg->type, "module") == 0);

    // if package has exports, use exports resolution
    if (pkg->has_exports && subpath) {
        char sub[256];
        snprintf(sub, sizeof(sub), "./%s", subpath);
        const char* resolved = npm_resolve_exports(pkg, sub, conditions, condition_count);
        if (resolved) {
            char full[2048];
            snprintf(full, sizeof(full), "%s/%s", package_dir, resolved);
            if (file_is_file(full)) {
                npm_package_json_free(pkg);
                return mem_strdup(full, MEM_CAT_JS_RUNTIME);
            }
        }
    }

    npm_package_json_free(pkg);

    // fallback: try the subpath directly
    if (subpath) {
        char full[2048];
        snprintf(full, sizeof(full), "%s/%s", package_dir, subpath);
        char* resolved = try_file_extensions(full);
        if (resolved) return resolved;
        if (file_is_dir(full)) {
            return try_directory(full, conditions, condition_count, out_esm);
        }
    }

    return NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

NpmModuleResolution npm_resolve_module(const char* specifier,
                                       const char* from_dir,
                                       const char** conditions,
                                       int condition_count) {
    NpmModuleResolution res = {};

    if (!specifier || !from_dir) return res;

    // 1. relative specifiers: './', '../', '/'
    if (specifier[0] == '.' || specifier[0] == '/') {
        char base[2048];
        if (specifier[0] == '/') {
            snprintf(base, sizeof(base), "%s", specifier);
        } else {
            snprintf(base, sizeof(base), "%s/%s", from_dir, specifier);
        }

        // try as file
        char* resolved = try_file_extensions(base);
        if (resolved) {
            res.resolved_path = resolved;
            res.is_esm = is_esm_file(resolved);
            res.found = true;
            return res;
        }

        // try as directory
        resolved = try_directory(base, conditions, condition_count, &res.is_esm);
        if (resolved) {
            res.resolved_path = resolved;
            res.found = true;
            return res;
        }

        return res; // not found
    }

    // 2. bare specifiers: walk up node_modules
    // split specifier into package name and subpath
    // e.g. "lodash/fp" → name="lodash", subpath="fp"
    // e.g. "@scope/pkg/util" → name="@scope/pkg", subpath="util"
    char pkg_name[256] = "";
    const char* subpath = NULL;

    if (specifier[0] == '@') {
        // scoped package: @scope/name[/subpath]
        const char* first_slash = strchr(specifier, '/');
        if (!first_slash) {
            // invalid: @scope with no package name
            return res;
        }
        const char* second_slash = strchr(first_slash + 1, '/');
        if (second_slash) {
            int name_len = (int)(second_slash - specifier);
            snprintf(pkg_name, sizeof(pkg_name), "%.*s", name_len, specifier);
            subpath = second_slash + 1;
        } else {
            snprintf(pkg_name, sizeof(pkg_name), "%s", specifier);
        }
    } else {
        const char* slash = strchr(specifier, '/');
        if (slash) {
            int name_len = (int)(slash - specifier);
            snprintf(pkg_name, sizeof(pkg_name), "%.*s", name_len, specifier);
            subpath = slash + 1;
        } else {
            snprintf(pkg_name, sizeof(pkg_name), "%s", specifier);
        }
    }

    // walk up from from_dir looking for node_modules/<pkg_name>
    char dir[2048];
    strncpy(dir, from_dir, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    while (dir[0]) {
        char nm_path[2048];
        snprintf(nm_path, sizeof(nm_path), "%s/node_modules/%s", dir, pkg_name);

        if (file_is_dir(nm_path) || file_is_symlink(nm_path)) {
            res.package_dir = mem_strdup(nm_path, MEM_CAT_JS_RUNTIME);

            if (subpath) {
                // resolve subpath within the package
                char* resolved = resolve_package_subpath(nm_path, subpath,
                                                         conditions, condition_count,
                                                         &res.is_esm);
                if (resolved) {
                    res.resolved_path = resolved;
                    res.found = true;
                    return res;
                }
            } else {
                // resolve package root entry point
                char* resolved = try_directory(nm_path, conditions, condition_count,
                                               &res.is_esm);
                if (resolved) {
                    res.resolved_path = resolved;
                    res.found = true;
                    return res;
                }
            }
        }

        // go up one directory
        char* parent = file_path_dirname(dir);
        if (!parent || strcmp(parent, dir) == 0 || strcmp(parent, "/") == 0) {
            mem_free(parent);
            break;
        }
        strncpy(dir, parent, sizeof(dir) - 1);
        mem_free(parent);
    }

    return res; // not found
}

void npm_module_resolution_free(NpmModuleResolution* res) {
    if (!res) return;
    if (res->resolved_path) mem_free(res->resolved_path);
    if (res->package_dir) mem_free(res->package_dir);
    res->resolved_path = NULL;
    res->package_dir = NULL;
}
