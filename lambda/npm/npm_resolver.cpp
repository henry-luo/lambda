// npm_resolver.cpp — Dependency tree resolution with semver matching

#include "npm_resolver.h"
#include "npm_registry.h"
#include "npm_lockfile.h"
#include "semver.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal: resolution queue
// ---------------------------------------------------------------------------

typedef struct {
    char* name;
    char* range;
} ResolveTask;

// check if a package@version is already resolved
static bool is_resolved(NpmResolutionResult* result, const char* name, const char* version) {
    for (int i = 0; i < result->count; i++) {
        if (strcmp(result->packages[i].name, name) == 0 &&
            strcmp(result->packages[i].version, version) == 0) {
            return true;
        }
    }
    return false;
}

// check if a package name is already resolved (any version)
static bool has_package(NpmResolutionResult* result, const char* name) {
    for (int i = 0; i < result->count; i++) {
        if (strcmp(result->packages[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

// check if the already-resolved version satisfies a range
static bool resolved_satisfies(NpmResolutionResult* result, const char* name, const char* range_str) {
    for (int i = 0; i < result->count; i++) {
        if (strcmp(result->packages[i].name, name) == 0) {
            SemVer ver = semver_parse(result->packages[i].version);
            SemVerRange range = semver_range_parse(range_str);
            return semver_satisfies(&ver, &range);
        }
    }
    return false;
}

static void add_resolved(NpmResolutionResult* result, NpmRegistryVersion* ver) {
    if (result->count >= result->cap) {
        result->cap *= 2;
        result->packages = (NpmResolvedPackage*)mem_realloc(
            result->packages, result->cap * sizeof(NpmResolvedPackage), MEM_CAT_JS_RUNTIME);
    }

    NpmResolvedPackage* pkg = &result->packages[result->count++];
    memset(pkg, 0, sizeof(NpmResolvedPackage));
    pkg->name = mem_strdup(ver->name, MEM_CAT_JS_RUNTIME);
    pkg->version = mem_strdup(ver->version, MEM_CAT_JS_RUNTIME);
    pkg->tarball_url = ver->tarball_url ? mem_strdup(ver->tarball_url, MEM_CAT_JS_RUNTIME) : NULL;
    pkg->integrity = ver->integrity ? mem_strdup(ver->integrity, MEM_CAT_JS_RUNTIME) : NULL;

    if (ver->dep_count > 0) {
        pkg->dep_names = (char**)mem_calloc(ver->dep_count, sizeof(char*), MEM_CAT_JS_RUNTIME);
        pkg->dep_versions = (char**)mem_calloc(ver->dep_count, sizeof(char*), MEM_CAT_JS_RUNTIME);
        for (int i = 0; i < ver->dep_count; i++) {
            pkg->dep_names[i] = mem_strdup(ver->dep_names[i], MEM_CAT_JS_RUNTIME);
            pkg->dep_versions[i] = mem_strdup(ver->dep_ranges[i], MEM_CAT_JS_RUNTIME);
        }
        pkg->dep_count = ver->dep_count;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

NpmResolutionResult* npm_resolve_dependencies(
    const char** dep_names,
    const char** dep_ranges,
    int dep_count,
    const char* lockfile_path)
{
    NpmResolutionResult* result = (NpmResolutionResult*)mem_calloc(1, sizeof(NpmResolutionResult), MEM_CAT_JS_RUNTIME);
    result->cap = 64;
    result->packages = (NpmResolvedPackage*)mem_calloc(result->cap, sizeof(NpmResolvedPackage), MEM_CAT_JS_RUNTIME);

    // load lockfile if available
    NpmLockFile* lockfile = NULL;
    if (lockfile_path) {
        lockfile = npm_lockfile_read(lockfile_path);
    }

    // task queue (BFS)
    int queue_cap = 128;
    int queue_count = 0;
    ResolveTask* queue = (ResolveTask*)mem_calloc(queue_cap, sizeof(ResolveTask), MEM_CAT_JS_RUNTIME);

    // seed queue with top-level deps
    for (int i = 0; i < dep_count; i++) {
        if (queue_count >= queue_cap) {
            queue_cap *= 2;
            queue = (ResolveTask*)mem_realloc(queue, queue_cap * sizeof(ResolveTask), MEM_CAT_JS_RUNTIME);
        }
        queue[queue_count].name = mem_strdup(dep_names[i], MEM_CAT_JS_RUNTIME);
        queue[queue_count].range = mem_strdup(dep_ranges[i], MEM_CAT_JS_RUNTIME);
        queue_count++;
    }

    // BFS resolution
    int qi = 0;
    int max_iterations = 500;  // safety limit

    while (qi < queue_count && qi < max_iterations) {
        ResolveTask* task = &queue[qi++];

        // skip if already resolved with a compatible version
        if (has_package(result, task->name)) {
            if (resolved_satisfies(result, task->name, task->range)) {
                continue;
            }
            // version conflict — for now, skip (first-wins strategy like npm)
            log_info("npm resolver: version conflict for %s (range %s), using existing",
                     task->name, task->range);
            continue;
        }

        // check lockfile first
        if (lockfile) {
            char lock_key[256];
            // try exact key format "name@version"
            // iterate lockfile entries to find matching name
            for (int i = 0; i < lockfile->entry_count; i++) {
                const NpmLockEntry* le = &lockfile->entries[i];
                if (!le->name) continue;
                // extract name from key (e.g. "lodash@4.17.21")
                const char* at = strrchr(le->name, '@');
                if (!at) continue;
                int name_len = (int)(at - le->name);
                if ((int)strlen(task->name) != name_len) continue;
                if (strncmp(le->name, task->name, name_len) != 0) continue;

                // check if locked version satisfies the range
                if (le->version) {
                    SemVer ver = semver_parse(le->version);
                    SemVerRange range = semver_range_parse(task->range);
                    if (semver_satisfies(&ver, &range)) {
                        // use locked version — create a fake registry version
                        NpmRegistryVersion fake = {};
                        fake.name = (char*)task->name;
                        fake.version = le->version;
                        fake.tarball_url = le->resolved;
                        fake.integrity = le->integrity;
                        fake.dep_names = le->dep_names;
                        fake.dep_ranges = le->dep_versions;
                        fake.dep_count = le->dep_count;
                        add_resolved(result, &fake);

                        // enqueue transitive deps
                        for (int j = 0; j < le->dep_count; j++) {
                            if (queue_count >= queue_cap) {
                                queue_cap *= 2;
                                queue = (ResolveTask*)mem_realloc(queue, queue_cap * sizeof(ResolveTask), MEM_CAT_JS_RUNTIME);
                            }
                            queue[queue_count].name = mem_strdup(le->dep_names[j], MEM_CAT_JS_RUNTIME);
                            queue[queue_count].range = mem_strdup(le->dep_versions[j], MEM_CAT_JS_RUNTIME);
                            queue_count++;
                        }
                        goto next_task;
                    }
                }
            }
        }

        // resolve from registry
        {
            NpmRegistryVersion* ver = npm_registry_resolve_version(task->name, task->range);
            if (!ver) {
                char err_buf[256];
                snprintf(err_buf, sizeof(err_buf),
                         "could not resolve %s@%s", task->name, task->range);
                result->error = mem_strdup(err_buf, MEM_CAT_JS_RUNTIME);
                result->success = false;

                // cleanup queue
                for (int i = qi; i < queue_count; i++) {
                    mem_free(queue[i].name);
                    mem_free(queue[i].range);
                }
                // cleanup processed tasks
                for (int i = 0; i < qi; i++) {
                    mem_free(queue[i].name);
                    mem_free(queue[i].range);
                }
                mem_free(queue);
                if (lockfile) npm_lockfile_free(lockfile);
                return result;
            }

            add_resolved(result, ver);

            // enqueue transitive dependencies
            for (int i = 0; i < ver->dep_count; i++) {
                if (queue_count >= queue_cap) {
                    queue_cap *= 2;
                    queue = (ResolveTask*)mem_realloc(queue, queue_cap * sizeof(ResolveTask), MEM_CAT_JS_RUNTIME);
                }
                queue[queue_count].name = mem_strdup(ver->dep_names[i], MEM_CAT_JS_RUNTIME);
                queue[queue_count].range = mem_strdup(ver->dep_ranges[i], MEM_CAT_JS_RUNTIME);
                queue_count++;
            }

            npm_registry_version_free(ver);
        }

        next_task:;
    }

    // cleanup queue
    for (int i = 0; i < queue_count; i++) {
        mem_free(queue[i].name);
        mem_free(queue[i].range);
    }
    mem_free(queue);
    if (lockfile) npm_lockfile_free(lockfile);

    result->success = true;
    log_info("npm resolver: resolved %d packages", result->count);
    return result;
}

void npm_resolution_free(NpmResolutionResult* result) {
    if (!result) return;
    for (int i = 0; i < result->count; i++) {
        NpmResolvedPackage* pkg = &result->packages[i];
        if (pkg->name) mem_free(pkg->name);
        if (pkg->version) mem_free(pkg->version);
        if (pkg->tarball_url) mem_free(pkg->tarball_url);
        if (pkg->integrity) mem_free(pkg->integrity);
        for (int j = 0; j < pkg->dep_count; j++) {
            if (pkg->dep_names[j]) mem_free(pkg->dep_names[j]);
            if (pkg->dep_versions[j]) mem_free(pkg->dep_versions[j]);
        }
        if (pkg->dep_names) mem_free(pkg->dep_names);
        if (pkg->dep_versions) mem_free(pkg->dep_versions);
    }
    if (result->packages) mem_free(result->packages);
    if (result->error) mem_free(result->error);
    mem_free(result);
}
