// npm_resolver.cpp — Dependency tree resolution with semver matching

#include "npm_resolver.h"
#include "npm_registry.h"
#include "npm_lockfile.h"
#include "semver.h"
#include "../../../lib/log.h"
#include "../../../lib/mem_grow.hpp"
#include "../../../lib/memtrack.h"

#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal: resolution queue
// ---------------------------------------------------------------------------

typedef struct {
    char* name;
    char* range;
} ResolveTask;

static void resolve_task_list_free(ResolveTask* tasks, int task_count) {
    for (int i = 0; i < task_count; i++) {
        mem_free(tasks[i].name);
        mem_free(tasks[i].range);
    }
    mem_free(tasks);
}

static bool resolve_task_append(ResolveTask** tasks, int* task_count, int* task_capacity,
                                const char* name, const char* range) {
    if (!lam::mem_grow_array(tasks, task_capacity, *task_count + 1, 128,
                             MEM_CAT_JS_RUNTIME)) {
        return false;
    }
    (*tasks)[*task_count].name = mem_strdup(name, MEM_CAT_JS_RUNTIME);
    (*tasks)[*task_count].range = mem_strdup(range, MEM_CAT_JS_RUNTIME);
    (*task_count)++;
    return true;
}

static NpmResolutionResult* npm_resolution_fail(NpmResolutionResult* result,
                                                ResolveTask* queue, int queue_count,
                                                NpmLockFile* lockfile,
                                                const char* message) {
    resolve_task_list_free(queue, queue_count);
    if (lockfile) npm_lockfile_free(lockfile);
    result->error = mem_strdup(message, MEM_CAT_JS_RUNTIME);
    result->success = false;
    return result;
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

static bool add_resolved(NpmResolutionResult* result, const NpmRegistryVersion* ver) {
    if (!lam::mem_grow_array(&result->packages, &result->cap, result->count + 1, 64,
                             MEM_CAT_JS_RUNTIME)) {
        return false;
    }

    NpmResolvedPackage* pkg = &result->packages[result->count++];
    memset(pkg, 0, sizeof(*pkg));
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
    return true;
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
    if (!result) return NULL;

    // load lockfile if available
    NpmLockFile* lockfile = NULL;
    if (lockfile_path) {
        lockfile = npm_lockfile_read(lockfile_path);
    }

    // task queue (BFS)
    int queue_cap = 0;
    int queue_count = 0;
    ResolveTask* queue = NULL;

    // seed queue with top-level deps
    for (int i = 0; i < dep_count; i++) {
        if (!resolve_task_append(&queue, &queue_count, &queue_cap, dep_names[i], dep_ranges[i])) {
            return npm_resolution_fail(result, queue, queue_count, lockfile,
                                       "out of memory while creating dependency queue");
        }
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
                        if (!add_resolved(result, &fake)) {
                            return npm_resolution_fail(result, queue, queue_count, lockfile,
                                                       "out of memory while recording resolved package");
                        }

                        // enqueue transitive deps
                        for (int j = 0; j < le->dep_count; j++) {
                            if (!resolve_task_append(&queue, &queue_count, &queue_cap,
                                                     le->dep_names[j], le->dep_versions[j])) {
                                return npm_resolution_fail(result, queue, queue_count, lockfile,
                                                           "out of memory while extending dependency queue");
                            }
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
                return npm_resolution_fail(result, queue, queue_count, lockfile, err_buf);
            }

            if (!add_resolved(result, ver)) {
                npm_registry_version_free(ver);
                return npm_resolution_fail(result, queue, queue_count, lockfile,
                                           "out of memory while recording resolved package");
            }

            // enqueue transitive dependencies
            for (int i = 0; i < ver->dep_count; i++) {
                if (!resolve_task_append(&queue, &queue_count, &queue_cap,
                                         ver->dep_names[i], ver->dep_ranges[i])) {
                    npm_registry_version_free(ver);
                    return npm_resolution_fail(result, queue, queue_count, lockfile,
                                               "out of memory while extending dependency queue");
                }
            }

            npm_registry_version_free(ver);
        }

        next_task:;
    }

    resolve_task_list_free(queue, queue_count);
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
