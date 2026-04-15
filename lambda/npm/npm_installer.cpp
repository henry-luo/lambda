// npm_installer.cpp — Download, extract, and link packages (pnpm-style layout)

#include "npm_installer.h"
#include "npm_resolver.h"
#include "npm_registry.h"
#include "npm_lockfile.h"
#include "npm_package_json.h"
#include "npm_tarball.h"
#include "../lib/file.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------

// pnpm-style layout:
//   node_modules/.lambda/<name>@<version>/node_modules/<name>/ → extracted files
//   node_modules/<name> → .lambda/<name>@<version>/node_modules/<name>  (symlink)

static void make_store_dir(char* out, int out_size, const char* project_dir,
                           const char* name, const char* version) {
    snprintf(out, out_size, "%s/node_modules/.lambda/%s@%s/node_modules/%s",
             project_dir, name, version, name);
}

static void make_top_link(char* out, int out_size, const char* project_dir,
                          const char* name) {
    snprintf(out, out_size, "%s/node_modules/%s", project_dir, name);
}

static void make_link_target(char* out, int out_size,
                             const char* name, const char* version) {
    // relative from node_modules/<name> → .lambda/<name>@<version>/node_modules/<name>
    // handle scoped packages: @scope/pkg → relative path needs to go up one more level
    if (name[0] == '@') {
        snprintf(out, out_size, "../.lambda/%s@%s/node_modules/%s",
                 name, version, name);
    } else {
        snprintf(out, out_size, ".lambda/%s@%s/node_modules/%s",
                 name, version, name);
    }
}

// ---------------------------------------------------------------------------
// Install a single resolved package
// ---------------------------------------------------------------------------

static int install_one_package(const char* project_dir,
                               const NpmResolvedPackage* pkg,
                               NpmInstallResult* result) {
    char store_dir[2048];
    make_store_dir(store_dir, sizeof(store_dir), project_dir, pkg->name, pkg->version);

    // check if already installed
    char pkg_json_path[2048];
    snprintf(pkg_json_path, sizeof(pkg_json_path), "%s/package.json", store_dir);
    if (file_exists(pkg_json_path)) {
        log_info("npm install: %s@%s already installed", pkg->name, pkg->version);
        result->packages_cached++;
        return 0;
    }

    // download tarball
    if (!pkg->tarball_url) {
        log_error("npm install: no tarball URL for %s@%s", pkg->name, pkg->version);
        return -1;
    }

    char* cached_tgz = npm_registry_download_tarball(pkg->tarball_url, pkg->integrity);
    if (!cached_tgz) {
        log_error("npm install: failed to download %s@%s", pkg->name, pkg->version);
        return -1;
    }

    // extract to store directory
    file_ensure_dir(store_dir);
    int ret = npm_extract_tarball(cached_tgz, store_dir);
    mem_free(cached_tgz);

    if (ret < 0) {
        log_error("npm install: failed to extract %s@%s", pkg->name, pkg->version);
        return -1;
    }

    result->packages_installed++;
    log_info("npm install: installed %s@%s", pkg->name, pkg->version);
    return 0;
}

// create top-level symlink for a package
static int link_package(const char* project_dir, const char* name, const char* version) {
    char link_path[2048];
    make_top_link(link_path, sizeof(link_path), project_dir, name);

    // for scoped packages (@scope/pkg), ensure the scope directory exists
    if (name[0] == '@') {
        char scope_dir[2048];
        snprintf(scope_dir, sizeof(scope_dir), "%s/node_modules/", project_dir);
        // extract scope part
        const char* slash = strchr(name, '/');
        if (slash) {
            int scope_len = (int)(slash - name);
            char scope[256];
            snprintf(scope, sizeof(scope), "%.*s", scope_len, name);
            char scope_path[2048];
            snprintf(scope_path, sizeof(scope_path), "%s/node_modules/%s", project_dir, scope);
            file_ensure_dir(scope_path);
        }
    }

    // remove existing symlink/file
    if (file_exists(link_path) || file_is_symlink(link_path)) {
        file_delete(link_path);
    }

    char target[2048];
    make_link_target(target, sizeof(target), name, version);
    int ret = file_symlink(target, link_path);
    if (ret < 0) {
        log_error("npm install: failed to create symlink %s → %s", link_path, target);
    }
    return ret;
}

// create inter-package dependency symlinks within the store
static void link_store_deps(const char* project_dir,
                            const NpmResolvedPackage* pkg,
                            const NpmResolutionResult* resolution) {
    // create symlinks for each dependency inside the package's own node_modules
    // e.g. node_modules/.lambda/express@4.18.2/node_modules/body-parser →
    //      node_modules/.lambda/body-parser@1.20.2/node_modules/body-parser
    for (int i = 0; i < pkg->dep_count; i++) {
        const char* dep_name = pkg->dep_names[i];

        // find the resolved version
        const char* dep_version = NULL;
        for (int j = 0; j < resolution->count; j++) {
            if (strcmp(resolution->packages[j].name, dep_name) == 0) {
                dep_version = resolution->packages[j].version;
                break;
            }
        }
        if (!dep_version) continue;

        // create symlink: .lambda/<pkg>@<ver>/node_modules/<dep> → .lambda/<dep>@<depver>/node_modules/<dep>
        char link_path[2048];
        snprintf(link_path, sizeof(link_path),
                 "%s/node_modules/.lambda/%s@%s/node_modules/%s",
                 project_dir, pkg->name, pkg->version, dep_name);

        // skip if it's the package itself (self-reference)
        if (strcmp(dep_name, pkg->name) == 0) continue;

        // skip if already exists
        if (file_exists(link_path) || file_is_symlink(link_path)) continue;

        // handle scoped dep names in link path
        if (dep_name[0] == '@') {
            const char* slash = strchr(dep_name, '/');
            if (slash) {
                char scope_dir[2048];
                int scope_len = (int)(slash - dep_name);
                snprintf(scope_dir, sizeof(scope_dir),
                         "%s/node_modules/.lambda/%s@%s/node_modules/%.*s",
                         project_dir, pkg->name, pkg->version, scope_len, dep_name);
                file_ensure_dir(scope_dir);
            }
        }

        // relative target: ../../../<dep>@<depver>/node_modules/<dep>
        char target[2048];
        snprintf(target, sizeof(target), "../../../%s@%s/node_modules/%s",
                 dep_name, dep_version, dep_name);

        file_symlink(target, link_path);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

NpmInstallResult* npm_install(const char* project_dir, const NpmInstallOptions* opts) {
    NpmInstallResult* result = (NpmInstallResult*)mem_calloc(1, sizeof(NpmInstallResult), MEM_CAT_JS_RUNTIME);

    // read package.json
    char pkg_json_path[2048];
    snprintf(pkg_json_path, sizeof(pkg_json_path), "%s/package.json", project_dir);

    NpmPackageJson* pkg_json = npm_package_json_parse(pkg_json_path);
    if (!pkg_json) {
        result->error = mem_strdup("no package.json found", MEM_CAT_JS_RUNTIME);
        return result;
    }

    // collect all dependency names and ranges
    int total_deps = pkg_json->dep_count;
    if (!opts || !opts->production) {
        total_deps += pkg_json->dev_dep_count;
    }

    if (total_deps == 0) {
        printf("No dependencies to install.\n");
        result->success = true;
        npm_package_json_free(pkg_json);
        return result;
    }

    const char** names = (const char**)mem_calloc(total_deps, sizeof(char*), MEM_CAT_JS_RUNTIME);
    const char** ranges = (const char**)mem_calloc(total_deps, sizeof(char*), MEM_CAT_JS_RUNTIME);
    int idx = 0;

    for (int i = 0; i < pkg_json->dep_count; i++) {
        names[idx] = pkg_json->dependencies[i].name;
        ranges[idx] = pkg_json->dependencies[i].range;
        idx++;
    }
    if (!opts || !opts->production) {
        for (int i = 0; i < pkg_json->dev_dep_count; i++) {
            names[idx] = pkg_json->dev_dependencies[i].name;
            ranges[idx] = pkg_json->dev_dependencies[i].range;
            idx++;
        }
    }

    // resolve dependency tree
    char lockfile_path[2048];
    snprintf(lockfile_path, sizeof(lockfile_path), "%s/lambda-node.lock", project_dir);
    const char* lf_path = file_exists(lockfile_path) ? lockfile_path : NULL;

    printf("Resolving dependencies...\n");
    NpmResolutionResult* resolution = npm_resolve_dependencies(names, ranges, idx, lf_path);

    mem_free(names);
    mem_free(ranges);

    if (!resolution || !resolution->success) {
        result->error = mem_strdup(
            resolution ? resolution->error : "resolution failed",
            MEM_CAT_JS_RUNTIME);
        if (resolution) npm_resolution_free(resolution);
        npm_package_json_free(pkg_json);
        return result;
    }

    printf("Installing %d packages...\n", resolution->count);

    // ensure node_modules/.lambda/ exists
    char lambda_dir[2048];
    snprintf(lambda_dir, sizeof(lambda_dir), "%s/node_modules/.lambda", project_dir);
    file_ensure_dir(lambda_dir);

    // install each resolved package
    for (int i = 0; i < resolution->count; i++) {
        if (opts && opts->dry_run) {
            printf("  %s@%s\n", resolution->packages[i].name, resolution->packages[i].version);
            continue;
        }
        int ret = install_one_package(project_dir, &resolution->packages[i], result);
        if (ret < 0) {
            result->error = mem_strdup("installation failed", MEM_CAT_JS_RUNTIME);
            npm_resolution_free(resolution);
            npm_package_json_free(pkg_json);
            return result;
        }
    }

    if (!opts || !opts->dry_run) {
        // create inter-package symlinks
        for (int i = 0; i < resolution->count; i++) {
            link_store_deps(project_dir, &resolution->packages[i], resolution);
        }

        // create top-level symlinks for direct dependencies
        for (int i = 0; i < pkg_json->dep_count; i++) {
            const char* name = pkg_json->dependencies[i].name;
            // find resolved version
            for (int j = 0; j < resolution->count; j++) {
                if (strcmp(resolution->packages[j].name, name) == 0) {
                    link_package(project_dir, name, resolution->packages[j].version);
                    break;
                }
            }
        }
        if (!opts || !opts->production) {
            for (int i = 0; i < pkg_json->dev_dep_count; i++) {
                const char* name = pkg_json->dev_dependencies[i].name;
                for (int j = 0; j < resolution->count; j++) {
                    if (strcmp(resolution->packages[j].name, name) == 0) {
                        link_package(project_dir, name, resolution->packages[j].version);
                        break;
                    }
                }
            }
        }

        // write/update lock file
        NpmLockFile* lockfile = npm_lockfile_create();
        for (int i = 0; i < resolution->count; i++) {
            NpmResolvedPackage* rpkg = &resolution->packages[i];
            char key[256];
            snprintf(key, sizeof(key), "%s@%s", rpkg->name, rpkg->version);
            npm_lockfile_add(lockfile, key, rpkg->version, rpkg->tarball_url,
                           rpkg->integrity,
                           (const char**)rpkg->dep_names,
                           (const char**)rpkg->dep_versions,
                           rpkg->dep_count);
        }
        npm_lockfile_write(lockfile, lockfile_path);
        npm_lockfile_free(lockfile);

        printf("\nInstalled %d packages (%d cached)\n",
               result->packages_installed, result->packages_cached);
    }

    result->success = true;
    npm_resolution_free(resolution);
    npm_package_json_free(pkg_json);
    return result;
}

NpmInstallResult* npm_install_package(const char* project_dir, const char* package_name,
                                      const char* version_range, bool is_dev,
                                      const NpmInstallOptions* opts) {
    NpmInstallResult* result = (NpmInstallResult*)mem_calloc(1, sizeof(NpmInstallResult), MEM_CAT_JS_RUNTIME);

    const char* range = version_range ? version_range : "*";

    // resolve single package + deps
    const char* names[] = { package_name };
    const char* ranges[] = { range };

    printf("Resolving %s@%s...\n", package_name, range);
    NpmResolutionResult* resolution = npm_resolve_dependencies(names, ranges, 1, NULL);

    if (!resolution || !resolution->success) {
        result->error = mem_strdup(
            resolution ? resolution->error : "resolution failed",
            MEM_CAT_JS_RUNTIME);
        if (resolution) npm_resolution_free(resolution);
        return result;
    }

    // ensure node_modules layout
    char lambda_dir[2048];
    snprintf(lambda_dir, sizeof(lambda_dir), "%s/node_modules/.lambda", project_dir);
    file_ensure_dir(lambda_dir);

    // install packages
    for (int i = 0; i < resolution->count; i++) {
        if (opts && opts->dry_run) {
            printf("  %s@%s\n", resolution->packages[i].name, resolution->packages[i].version);
            continue;
        }
        install_one_package(project_dir, &resolution->packages[i], result);
    }

    if (!opts || !opts->dry_run) {
        // create symlinks
        for (int i = 0; i < resolution->count; i++) {
            link_store_deps(project_dir, &resolution->packages[i], resolution);
        }
        // top-level link for the requested package
        for (int j = 0; j < resolution->count; j++) {
            if (strcmp(resolution->packages[j].name, package_name) == 0) {
                link_package(project_dir, package_name, resolution->packages[j].version);
                break;
            }
        }

        // update package.json with new dependency
        char pkg_json_path[2048];
        snprintf(pkg_json_path, sizeof(pkg_json_path), "%s/package.json", project_dir);

        // find resolved version
        const char* resolved_version = NULL;
        for (int i = 0; i < resolution->count; i++) {
            if (strcmp(resolution->packages[i].name, package_name) == 0) {
                resolved_version = resolution->packages[i].version;
                break;
            }
        }

        if (resolved_version) {
            // read existing package.json or create minimal one
            char* content = read_text_file(pkg_json_path);
            if (!content) {
                // create minimal package.json
                char buf[512];
                if (is_dev) {
                    snprintf(buf, sizeof(buf),
                             "{\n  \"devDependencies\": {\n    \"%s\": \"^%s\"\n  }\n}\n",
                             package_name, resolved_version);
                } else {
                    snprintf(buf, sizeof(buf),
                             "{\n  \"dependencies\": {\n    \"%s\": \"^%s\"\n  }\n}\n",
                             package_name, resolved_version);
                }
                write_text_file(pkg_json_path, buf);
            } else {
                // TODO: proper JSON editing to add/update dependency
                // For now, log what should be added
                printf("Add to package.json %s: \"%s\": \"^%s\"\n",
                       is_dev ? "devDependencies" : "dependencies",
                       package_name, resolved_version);
                mem_free(content);
            }
        }
    }

    printf("Installed %s@%s (%d packages total)\n", package_name,
           resolution->count > 0 ? resolution->packages[0].version : "?",
           resolution->count);

    result->success = true;
    npm_resolution_free(resolution);
    return result;
}

int npm_uninstall(const char* project_dir, const char* package_name) {
    // remove top-level symlink
    char link_path[2048];
    make_top_link(link_path, sizeof(link_path), project_dir, package_name);

    if (file_is_symlink(link_path)) {
        file_delete(link_path);
    }

    // TODO: remove from package.json, clean unused packages from .lambda/
    printf("Removed %s\n", package_name);
    return 0;
}

void npm_install_result_free(NpmInstallResult* result) {
    if (!result) return;
    if (result->error) mem_free(result->error);
    mem_free(result);
}
