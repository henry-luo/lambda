#include "module_ast_prebuild.hpp"
#include "compiler_worker_stack.h"

#include "lib/file.h"
#include "lib/hashmap_typed.hpp"
#include "lib/log.h"
#include "lib/mem.h"
#include "lib/shell.h"
#include "lib/thread_pool.h"
#include "lib/path_str.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// AST prebuild cannot own a closure-local pool: independent roots and nested
// import discoveries must rendezvous at the same canonical module future.
// The cache still validates source identity; this registry only schedules the
// best-effort AST producer and its dependency continuations.

typedef struct ModuleAstPrebuildRegistry ModuleAstPrebuildRegistry;
typedef struct ModuleAstPrebuildTask ModuleAstPrebuildTask;

typedef enum ModuleAstPrebuildJobKind {
    MODULE_AST_PREBUILD_DISCOVER_TASK,
    MODULE_AST_PREBUILD_BUILD_TASK,
    MODULE_AST_PREBUILD_DISCOVER_ROOT,
} ModuleAstPrebuildJobKind;

typedef struct ModuleAstPrebuildRoot {
    const ModuleAstPrebuildProfile* profile;
    ModuleAstPrebuildScope* scope;
    char* path;
    char* source;
} ModuleAstPrebuildRoot;

typedef struct ModuleAstPrebuildJob {
    ModuleAstPrebuildRegistry* registry;
    ModuleAstPrebuildTask* task;
    ModuleAstPrebuildRoot* root;
    ModuleAstPrebuildJobKind kind;
    ModuleAstPrebuildScope* scope;
} ModuleAstPrebuildJob;

struct ModuleAstPrebuildTask {
    const ModuleAstPrebuildProfile* profile;
    ModuleAstPrebuildScope* scope;
    char* key;
    char* path;
    ArrayList* dependencies;  // ModuleAstPrebuildTask*
    ArrayList* dependents;    // ModuleAstPrebuildTask*
    uint32_t pending_dependencies;
    uint32_t visit_mark;
    bool discovery_queued;
    bool discovery_complete;
    bool build_queued;
    bool complete;
    bool failed;
    bool dependency_failed;
    pthread_cond_t completed;
};

typedef struct ModuleAstPrebuildTaskEntry {
    const char* key;
    ModuleAstPrebuildTask* task;
} ModuleAstPrebuildTaskEntry;

struct ModuleAstPrebuildScope {
    ModuleAstPrebuildRegistry* registry;
    ModuleAstPrebuildProfiles profiles;
    SecurityIOClient io;
    ArrayList* tasks;
    uint64_t serial;
    uint64_t jobs;
    bool closing;
    pthread_cond_t idle;
};

typedef TypedHashMap<ModuleAstPrebuildTaskEntry,
    HashMapCStrMemberKeyOps<ModuleAstPrebuildTaskEntry,
        &ModuleAstPrebuildTaskEntry::key>> ModuleAstPrebuildTaskMap;

struct ModuleAstPrebuildRegistry {
    ModuleAstPrebuildTaskMap tasks;
    const ModuleAstPrebuildProfile* profiles[MODULE_AST_LANGUAGE_COUNT];
    ThreadPool* pool;
    ArrayList* scopes;
    uint64_t next_scope;
    pthread_mutex_t mutex;
    uint32_t next_visit_mark;
    bool ready;
};

static ModuleAstPrebuildRegistry g_module_ast_prebuild_registry = {};
static pthread_once_t g_module_ast_prebuild_registry_once = PTHREAD_ONCE_INIT;

static bool module_ast_prebuild_enabled(void) {
    const char* value = shell_getenv("LAMBDA_MODULE_AST_PREBUILD");
    return !value || (strcmp(value, "0") != 0 && strcmp(value, "off") != 0 &&
        strcmp(value, "false") != 0);
}

static int module_ast_prebuild_pool_workers(void) {
    const char* value = shell_getenv("LAMBDA_MODULE_AST_THREADS");
    int requested = value ? atoi(value) : 0;
    if (requested <= 0) return 8;
    return requested > 8 ? 8 : requested;
}

static void module_ast_prebuild_registry_init(void) {
    ModuleAstPrebuildRegistry* registry = &g_module_ast_prebuild_registry;
    if (pthread_mutex_init(&registry->mutex, NULL) != 0) {
        log_error("module-ast-prebuild: could not initialize registry mutex");
        return;
    }
    if (!registry->tasks.init(256)) {
        log_error("module-ast-prebuild: could not initialize task registry");
        pthread_mutex_destroy(&registry->mutex);
        return;
    }
    registry->pool = tp_create_with_stack(module_ast_prebuild_pool_workers(),
        LAMBDA_COMPILER_WORKER_STACK_BYTES);
    if (!registry->pool) {
        // The caller still gets a correct serial best-effort producer; normal
        // loading remains authoritative if a scheduling allocation fails.
        log_warn("module-ast-prebuild: global pool unavailable; running jobs serially");
    }
    registry->scopes = arraylist_new(0);
    if (!registry->scopes) {
        tp_destroy(registry->pool);
        registry->tasks.destroy();
        pthread_mutex_destroy(&registry->mutex);
        return;
    }
    registry->next_scope = 1;
    registry->ready = true;
}

static ModuleAstPrebuildRegistry* module_ast_prebuild_registry_get(void) {
    pthread_once(&g_module_ast_prebuild_registry_once,
        module_ast_prebuild_registry_init);
    return g_module_ast_prebuild_registry.ready
        ? &g_module_ast_prebuild_registry : NULL;
}

static void module_ast_prebuild_free_task(ModuleAstPrebuildTask* task) {
    if (!task) return;
    pthread_cond_destroy(&task->completed);
    arraylist_free(task->dependencies);
    arraylist_free(task->dependents);
    mem_free(task->key);
    mem_free(task->path);
    mem_free(task);
}

static void module_ast_prebuild_free_specs(ArrayList* specs) {
    if (!specs) return;
    for (int index = 0; index < specs->length; index++) {
        mem_free(specs->data[index]);
    }
    arraylist_free(specs);
}

static void module_ast_prebuild_free_root(ModuleAstPrebuildRoot* root) {
    if (!root) return;
    mem_free(root->path);
    mem_free(root->source);
    mem_free(root);
}

static char* module_ast_prebuild_canonical_path(char* path, ModuleAstPrebuildScope* scope) {
    if (!path) return NULL;
    if (scope) {
        // S2.4.4: lexical resolution performs no native filesystem probes.
        char normalized[4096];
        char* result = path_str_normalize_lexical_posix(path, normalized, sizeof(normalized), false) >= 0
            ? mem_strdup(normalized, MEM_CAT_SYSTEM) : NULL;
        mem_free(path);
        if (result && scope->io.canonical_source) {
            char* canonical = NULL;
            (void)security_io_source_path(&scope->io, result, &canonical, NULL);
            mem_free(result); result = canonical;
        }
        return result;
    }
    char* canonical = file_realpath(path);
    if (!canonical) return path;
    mem_free(path);
    return canonical;
}

static char* module_ast_prebuild_task_key(const ModuleAstPrebuildProfile* profile,
        const char* path, ModuleAstPrebuildScope* scope) {
    if (!profile || !path) return NULL;
    const char* name = profile->name ? profile->name : "<unnamed>";
    size_t name_length = strlen(name);
    size_t path_length = strlen(path);
    if (name_length > SIZE_MAX - path_length - 64) return NULL;
    size_t capacity = name_length + path_length + 64;
    char* key = (char*)mem_alloc(capacity, MEM_CAT_SYSTEM);
    if (!key) return NULL;
    int written = snprintf(key, capacity, "%llu:%u:%s:%s",
        (unsigned long long)(scope ? scope->serial : 0), (unsigned)profile->language, name, path);
    if (written < 0 || (size_t)written >= capacity) {
        mem_free(key);
        return NULL;
    }
    return key;
}

// registry->mutex must be held. The profiles are static descriptors owned by
// their language adapters, so a registry lifetime may safely borrow them.
static bool module_ast_prebuild_register_profiles_locked(
        ModuleAstPrebuildRegistry* registry,
        const ModuleAstPrebuildProfiles* profiles) {
    if (!registry || !profiles) return false;
    for (int language = 0; language < MODULE_AST_LANGUAGE_COUNT; language++) {
        const ModuleAstPrebuildProfile* profile = profiles->profiles[language];
        if (!profile) continue;
        if (profile->language != (ModuleAstLanguage)language) return false;
        // A profile name participates in the task key. Keep the first profile
        // only as a cross-language fallback; same-language children retain the
        // requesting profile so test and hosted adapters do not collide.
        if (!registry->profiles[language]) registry->profiles[language] = profile;
    }
    return true;
}

static bool module_ast_prebuild_register_profile(
        ModuleAstPrebuildRegistry* registry,
        const ModuleAstPrebuildProfile* profile) {
    if (!registry || !profile || profile->language >= MODULE_AST_LANGUAGE_COUNT) {
        return false;
    }
    pthread_mutex_lock(&registry->mutex);
    if (!registry->profiles[profile->language]) {
        registry->profiles[profile->language] = profile;
    }
    pthread_mutex_unlock(&registry->mutex);
    return true;
}

static void module_ast_prebuild_notify_dependents(
    ModuleAstPrebuildRegistry* registry, ModuleAstPrebuildTask* task,
    bool success);
static void module_ast_prebuild_activate_task(
    ModuleAstPrebuildRegistry* registry, ModuleAstPrebuildTask* task);

static void module_ast_prebuild_finish_task(ModuleAstPrebuildRegistry* registry,
        ModuleAstPrebuildTask* task, bool success) {
    if (!registry || !task) return;
    bool notify = false;
    pthread_mutex_lock(&registry->mutex);
    if (!task->complete) {
        task->complete = true;
        task->failed = !success;
        pthread_cond_broadcast(&task->completed);
        notify = true;
    }
    pthread_mutex_unlock(&registry->mutex);
    if (notify) module_ast_prebuild_notify_dependents(registry, task, success);
}

static void module_ast_prebuild_run_job(void* opaque);

static bool module_ast_prebuild_submit_job(ModuleAstPrebuildRegistry* registry,
        ModuleAstPrebuildJob* job) {
    if (!registry || !job) return false;
    if (job->scope) {
        pthread_mutex_lock(&registry->mutex);
        bool accepting = !job->scope->closing;
        if (accepting) job->scope->jobs++;
        pthread_mutex_unlock(&registry->mutex);
        if (!accepting) {
            module_ast_prebuild_finish_task(registry, job->task, false);
            if (job->root) module_ast_prebuild_free_root(job->root);
            mem_free(job);
            return false;
        }
    }
    if (registry->pool && tp_submit(registry->pool, module_ast_prebuild_run_job,
            job)) return true;
    // A submission failure must not strand direct waiters. The serial fallback
    // is reached only when the process-global pool cannot accept work.
    module_ast_prebuild_run_job(job);
    return true;
}

static void module_ast_prebuild_queue_task(ModuleAstPrebuildRegistry* registry,
        ModuleAstPrebuildTask* task, ModuleAstPrebuildJobKind kind) {
    ModuleAstPrebuildJob* job = (ModuleAstPrebuildJob*)mem_calloc(1,
        sizeof(ModuleAstPrebuildJob), MEM_CAT_SYSTEM);
    if (!job) {
        log_error("module-ast-prebuild: could not queue %s for %s",
            kind == MODULE_AST_PREBUILD_DISCOVER_TASK ? "discovery" : "build",
            task && task->path ? task->path : "<unknown>");
        module_ast_prebuild_finish_task(registry, task, false);
        return;
    }
    job->registry = registry;
    job->task = task;
    job->kind = kind;
    job->scope = task->scope;
    (void)module_ast_prebuild_submit_job(registry, job);
}

static ModuleAstPrebuildTask* module_ast_prebuild_request_task(
        ModuleAstPrebuildRegistry* registry,
        const ModuleAstPrebuildProfile* profile, char* path, ModuleAstPrebuildScope* scope) {
    if (!registry || !profile || !path) {
        mem_free(path);
        return NULL;
    }
    path = module_ast_prebuild_canonical_path(path, scope);
    char* key = module_ast_prebuild_task_key(profile, path, scope);
    if (!key) {
        mem_free(path);
        return NULL;
    }

    ModuleAstPrebuildTask* task = NULL;
    bool queue = false;
    pthread_mutex_lock(&registry->mutex);
    if (scope && scope->closing) {
        pthread_mutex_unlock(&registry->mutex); mem_free(key); mem_free(path); return NULL;
    }
    ModuleAstPrebuildTaskEntry probe = {key, NULL};
    const ModuleAstPrebuildTaskEntry* existing = registry->tasks.get(probe);
    if (existing) {
        task = existing->task;
        mem_free(key);
        mem_free(path);
    } else {
        task = (ModuleAstPrebuildTask*)mem_calloc(1, sizeof(*task), MEM_CAT_SYSTEM);
        if (task) {
            task->profile = profile;
            task->scope = scope;
            task->key = key;
            task->path = path;
            if (pthread_cond_init(&task->completed, NULL) == 0) {
                ModuleAstPrebuildTaskEntry entry = {task->key, task};
                // hashmap_set() returns a replaced entry, not the inserted one;
                // a NULL result is the normal new-key success case.
                registry->tasks.set(entry);
                if (!registry->tasks.oom() && (!scope || arraylist_append(scope->tasks, task))) {
                    task->discovery_queued = true;
                    queue = true;
                } else {
                    registry->tasks.erase(entry);
                    pthread_cond_destroy(&task->completed);
                    mem_free(task->path);
                    mem_free(task->key);
                    path = NULL;
                    key = NULL;
                    mem_free(task);
                    task = NULL;
                }
            } else {
                mem_free(task->path);
                mem_free(task->key);
                path = NULL;
                key = NULL;
                mem_free(task);
                task = NULL;
            }
        }
    }
    pthread_mutex_unlock(&registry->mutex);

    if (!task) {
        mem_free(key);
        mem_free(path);
        return NULL;
    }
    if (queue) module_ast_prebuild_queue_task(registry, task,
        MODULE_AST_PREBUILD_DISCOVER_TASK);
    return task;
}

// registry->mutex must be held.
static bool module_ast_prebuild_has_dependency_locked(
        ModuleAstPrebuildTask* parent, ModuleAstPrebuildTask* child) {
    for (int index = 0; parent && parent->dependencies &&
            index < parent->dependencies->length; index++) {
        if (parent->dependencies->data[index] == child) return true;
    }
    return false;
}

// registry->mutex must be held. Direct dependency links are immutable after
// publication, making a mark walk sufficient for prebuild-cycle rejection.
static bool module_ast_prebuild_reaches_locked(ModuleAstPrebuildTask* task,
        ModuleAstPrebuildTask* target, uint32_t visit_mark) {
    if (task == target) return true;
    if (!task || task->visit_mark == visit_mark) return false;
    task->visit_mark = visit_mark;
    for (int index = 0; task->dependencies && index < task->dependencies->length;
            index++) {
        if (module_ast_prebuild_reaches_locked(
                (ModuleAstPrebuildTask*)task->dependencies->data[index], target,
                visit_mark)) return true;
    }
    return false;
}

static bool module_ast_prebuild_add_dependency(ModuleAstPrebuildRegistry* registry,
        ModuleAstPrebuildTask* parent, ModuleAstPrebuildTask* child) {
    if (!registry || !parent || !child) return false;
    bool child_complete = false;
    bool appended = false;
    bool duplicate = false;
    bool cycle = false;
    pthread_mutex_lock(&registry->mutex);
    if (!parent->complete) {
        duplicate = module_ast_prebuild_has_dependency_locked(parent, child);
        if (!duplicate) {
            uint32_t visit_mark = ++registry->next_visit_mark;
            if (visit_mark == 0) visit_mark = ++registry->next_visit_mark;
            cycle = module_ast_prebuild_reaches_locked(child, parent, visit_mark);
            if (!cycle) {
                if (!parent->dependencies) parent->dependencies = arraylist_new(4);
                if (!child->complete && !child->dependents) {
                    child->dependents = arraylist_new(4);
                }
                if (parent->dependencies && (child->complete || child->dependents) &&
                        arraylist_append(parent->dependencies, child)) {
                    if (child->complete || arraylist_append(child->dependents, parent)) {
                        appended = true;
                        child_complete = child->complete;
                        if (!child_complete) parent->pending_dependencies++;
                        else if (child->failed) parent->dependency_failed = true;
                    } else {
                        arraylist_remove(parent->dependencies,
                            parent->dependencies->length - 1);
                    }
                }
            }
            if (cycle || !appended) parent->dependency_failed = true;
        }
    }
    pthread_mutex_unlock(&registry->mutex);

    if (cycle) {
        log_warn("module-ast-prebuild: cycle skipped %s -> %s",
            parent->path ? parent->path : "<unknown>",
            child->path ? child->path : "<unknown>");
        return false;
    }
    if (!appended && !duplicate) {
        log_error("module-ast-prebuild: could not record dependency %s -> %s",
            parent->path ? parent->path : "<unknown>",
            child->path ? child->path : "<unknown>");
        return false;
    }
    return true;
}

// Every task is process-lifetime owned, so the dependent list is stable once a
// task completes. Read one entry under the mutex at a time to avoid a temporary
// whole-list allocation on a completion path.
static void module_ast_prebuild_notify_dependents(
        ModuleAstPrebuildRegistry* registry, ModuleAstPrebuildTask* task,
        bool success) {
    if (!registry || !task) return;
    for (int index = 0;; index++) {
        ModuleAstPrebuildTask* parent = NULL;
        pthread_mutex_lock(&registry->mutex);
        if (task->dependents && index < task->dependents->length) {
            parent = (ModuleAstPrebuildTask*)task->dependents->data[index];
            if (!parent->complete && parent->pending_dependencies > 0) {
                parent->pending_dependencies--;
                if (!success) parent->dependency_failed = true;
            }
        }
        pthread_mutex_unlock(&registry->mutex);
        if (!parent) break;
        module_ast_prebuild_activate_task(registry, parent);
    }
}

// Called after discovery and after each direct child completion.
static void module_ast_prebuild_activate_task(ModuleAstPrebuildRegistry* registry,
        ModuleAstPrebuildTask* task) {
    bool queue_build = false;
    bool fail = false;
    pthread_mutex_lock(&registry->mutex);
    if (!task->complete && task->discovery_complete &&
            task->pending_dependencies == 0) {
        if (task->dependency_failed) {
            fail = true;
        } else if (!task->build_queued) {
            task->build_queued = true;
            queue_build = true;
        }
    }
    pthread_mutex_unlock(&registry->mutex);
    if (fail) module_ast_prebuild_finish_task(registry, task, false);
    if (queue_build) module_ast_prebuild_queue_task(registry, task,
        MODULE_AST_PREBUILD_BUILD_TASK);
}

static const ModuleAstPrebuildProfile* module_ast_prebuild_profile_for_language(
        ModuleAstPrebuildRegistry* registry,
        const ModuleAstPrebuildProfile* requester, ModuleAstLanguage language,
        ModuleAstPrebuildScope* scope) {
    if (!registry || language >= MODULE_AST_LANGUAGE_COUNT) return NULL;
    if (requester && requester->language == language) return requester;
    if (scope) return scope->profiles.profiles[language];
    pthread_mutex_lock(&registry->mutex);
    const ModuleAstPrebuildProfile* profile = registry->profiles[language];
    pthread_mutex_unlock(&registry->mutex);
    return profile;
}

// parent is NULL for a root discovery: roots seed work but do not own a future
// because the ordinary loader is already responsible for parsing that module.
static void module_ast_prebuild_discover_imports(
        ModuleAstPrebuildRegistry* registry,
        const ModuleAstPrebuildProfile* profile, const char* path,
        const char* source, ModuleAstPrebuildTask* parent, ModuleAstPrebuildScope* scope) {
    if (!registry || !profile || !path || !source) return;
    ArrayList* specifiers = profile->discover_imports
        ? profile->discover_imports(profile->context, source, strlen(source)) : NULL;
    for (int index = 0; specifiers && index < specifiers->length; index++) {
        const char* specifier = (const char*)specifiers->data[index];
        ModuleAstResolvedImport resolved = {};
        bool resolved_ok = specifier && (scope
            ? profile->resolve_authorized && profile->resolve_authorized(profile->context,
                path, specifier, &resolved, &scope->io)
            : profile->resolve_import && profile->resolve_import(profile->context, path, specifier, &resolved));
        if (!resolved_ok || !resolved.path) {
            mem_free(resolved.path);
            continue;
        }
        const ModuleAstPrebuildProfile* dependency_profile =
            module_ast_prebuild_profile_for_language(registry, profile,
                resolved.language, scope);
        if (!dependency_profile) {
            mem_free(resolved.path);
            continue;
        }
        ModuleAstPrebuildTask* child = module_ast_prebuild_request_task(registry,
            dependency_profile, resolved.path, scope);
        if (parent && (!child || !module_ast_prebuild_add_dependency(registry,
                parent, child))) {
            pthread_mutex_lock(&registry->mutex);
            parent->dependency_failed = true;
            pthread_mutex_unlock(&registry->mutex);
        }
    }
    module_ast_prebuild_free_specs(specifiers);
}

static void module_ast_prebuild_discover_task(ModuleAstPrebuildRegistry* registry,
        ModuleAstPrebuildTask* task) {
    if (!registry || !task || !task->profile) {
        module_ast_prebuild_finish_task(registry, task, false);
        return;
    }
    char* source = NULL; size_t length = 0;
    if (!security_io_read_bytes(task->scope ? &task->scope->io : NULL,
            task->path, true, UINT64_MAX, MEM_CAT_SYSTEM, &source, &length, NULL)) {
        log_error("module-ast-prebuild: could not read %s",
            task->path ? task->path : "<unknown>");
        module_ast_prebuild_finish_task(registry, task, false);
        return;
    }
    module_ast_prebuild_discover_imports(registry, task->profile, task->path,
        source, task, task->scope);
    mem_free(source);
    pthread_mutex_lock(&registry->mutex);
    if (!task->complete) task->discovery_complete = true;
    pthread_mutex_unlock(&registry->mutex);
    module_ast_prebuild_activate_task(registry, task);
}

static void module_ast_prebuild_build_task(ModuleAstPrebuildRegistry* registry,
        ModuleAstPrebuildTask* task) {
    bool success = false;
    if (task && task->profile && task->path) {
        success = task->scope
            ? task->profile->build_authorized && task->profile->build_authorized(
                task->profile->context, task->path, &task->scope->io)
            : task->profile->build_ast && task->profile->build_ast(task->profile->context, task->path);
    }
    module_ast_prebuild_finish_task(registry, task, success);
}

static void module_ast_prebuild_discover_root(ModuleAstPrebuildRegistry* registry,
        ModuleAstPrebuildRoot* root) {
    if (!registry || !root || !root->profile || !root->path) {
        module_ast_prebuild_free_root(root);
        return;
    }
    char* source = root->source;
    size_t length = 0;
    if (!source) (void)security_io_read_bytes(root->scope ? &root->scope->io : NULL,
        root->path, true, UINT64_MAX, MEM_CAT_SYSTEM, &source, &length, NULL);
    root->source = NULL;
    if (!source) {
        log_warn("module-ast-prebuild: root discovery could not read %s",
            root->path);
        module_ast_prebuild_free_root(root);
        return;
    }
    module_ast_prebuild_discover_imports(registry, root->profile, root->path,
        source, NULL, root->scope);
    mem_free(source);
    module_ast_prebuild_free_root(root);
}

static void module_ast_prebuild_run_job(void* opaque) {
    ModuleAstPrebuildJob* job = (ModuleAstPrebuildJob*)opaque;
    if (!job) return;
    if (job->kind == MODULE_AST_PREBUILD_DISCOVER_TASK) {
        module_ast_prebuild_discover_task(job->registry, job->task);
    } else if (job->kind == MODULE_AST_PREBUILD_BUILD_TASK) {
        module_ast_prebuild_build_task(job->registry, job->task);
    } else {
        module_ast_prebuild_discover_root(job->registry, job->root);
    }
    if (job->scope) {
        pthread_mutex_lock(&job->registry->mutex);
        if (--job->scope->jobs == 0) pthread_cond_broadcast(&job->scope->idle);
        pthread_mutex_unlock(&job->registry->mutex);
    }
    mem_free(job);
}

static bool module_ast_prebuild_imports_impl(const ModuleAstPrebuildProfiles* profiles,
        ModuleAstLanguage root_language, const char* root_path,
        const char* root_source, size_t root_source_length,
        ModuleAstPrebuildStats* out_stats, ModuleAstPrebuildScope* scope) {
    if (out_stats) memset(out_stats, 0, sizeof(*out_stats));
    if (!module_ast_prebuild_enabled() || !profiles ||
            root_language >= MODULE_AST_LANGUAGE_COUNT || !root_path ||
            !profiles->profiles[root_language]) return false;
    ModuleAstPrebuildRegistry* registry = module_ast_prebuild_registry_get();
    if (!registry) return false;

    pthread_mutex_lock(&registry->mutex);
    bool registered = scope ? !scope->closing
        : module_ast_prebuild_register_profiles_locked(registry, profiles);
    pthread_mutex_unlock(&registry->mutex);
    if (!registered) return false;

    ModuleAstPrebuildRoot* root = (ModuleAstPrebuildRoot*)mem_calloc(1,
        sizeof(ModuleAstPrebuildRoot), MEM_CAT_SYSTEM);
    if (!root) return false;
    root->profile = profiles->profiles[root_language];
    root->scope = scope;
    root->path = module_ast_prebuild_canonical_path(mem_strdup(root_path,
        MEM_CAT_SYSTEM), scope);
    root->source = root_source ? mem_dup_n(root_source, root_source_length,
        MEM_CAT_SYSTEM) : NULL;
    if (!root->path || (root_source && !root->source)) {
        module_ast_prebuild_free_root(root);
        return false;
    }

    ModuleAstPrebuildJob* job = (ModuleAstPrebuildJob*)mem_calloc(1,
        sizeof(ModuleAstPrebuildJob), MEM_CAT_SYSTEM);
    if (!job) {
        module_ast_prebuild_free_root(root);
        return false;
    }
    job->registry = registry;
    job->root = root;
    job->kind = MODULE_AST_PREBUILD_DISCOVER_ROOT;
    job->scope = scope;
    if (out_stats && registry->pool) out_stats->worker_pool_runs = 1;
    // Root submission deliberately has no wait-all: direct import consumers
    // rendezvous with only the task future they are about to consume.
    return module_ast_prebuild_submit_job(registry, job);
}

static bool module_ast_prebuild_await_impl(const ModuleAstPrebuildProfile* profile,
        const char* path, ModuleAstPrebuildScope* scope) {
    if (!module_ast_prebuild_enabled() || !profile || !path) return false;
    ModuleAstPrebuildRegistry* registry = module_ast_prebuild_registry_get();
    if (!registry || (!scope && !module_ast_prebuild_register_profile(registry, profile))) {
        return false;
    }
    ModuleAstPrebuildTask* task = module_ast_prebuild_request_task(registry,
        profile, mem_strdup(path, MEM_CAT_SYSTEM), scope);
    if (!task) return false;

    pthread_mutex_lock(&registry->mutex);
    while (!task->complete) {
        pthread_cond_wait(&task->completed, &registry->mutex);
    }
    bool success = !task->failed;
    pthread_mutex_unlock(&registry->mutex);
    return success;
}

bool module_ast_prebuild_imports(const ModuleAstPrebuildProfiles* profiles,
        ModuleAstLanguage language, const char* path, const char* source,
        size_t length, ModuleAstPrebuildStats* stats) {
    return module_ast_prebuild_imports_impl(profiles, language, path, source, length, stats, NULL);
}

bool module_ast_prebuild_await_import(const ModuleAstPrebuildProfile* profile, const char* path) {
    return module_ast_prebuild_await_impl(profile, path, NULL);
}

ModuleAstPrebuildScope* module_ast_prebuild_scope_open(
        const ModuleAstPrebuildProfiles* profiles, const SecurityIOClient* io) {
    if (!profiles || !io || io->version != 1 || !io->read) return NULL;
    for (int i = 0; i < MODULE_AST_LANGUAGE_COUNT; i++) {
        const ModuleAstPrebuildProfile* profile = profiles->profiles[i];
        if (profile && (profile->language != i || !profile->build_authorized || !profile->resolve_authorized)) return NULL;
    }
    ModuleAstPrebuildRegistry* registry = module_ast_prebuild_registry_get();
    if (!registry) return NULL;
    ModuleAstPrebuildScope* scope = (ModuleAstPrebuildScope*)mem_calloc(1, sizeof(*scope), MEM_CAT_SYSTEM);
    if (!scope) return NULL;
    scope->tasks = arraylist_new(0);
    if (!scope->tasks || pthread_cond_init(&scope->idle, NULL) != 0) {
        if (scope->tasks) arraylist_free(scope->tasks);
        mem_free(scope); return NULL;
    }
    scope->profiles = *profiles; scope->io = *io;
    pthread_mutex_lock(&registry->mutex);
    bool valid = registry->next_scope != UINT64_MAX && arraylist_append(registry->scopes, scope);
    if (valid) { scope->serial = registry->next_scope++; scope->registry = registry; }
    pthread_mutex_unlock(&registry->mutex);
    if (!valid) { pthread_cond_destroy(&scope->idle); arraylist_free(scope->tasks); mem_free(scope); return NULL; }
    return scope;
}

bool module_ast_prebuild_scope_imports(ModuleAstPrebuildScope* scope,
        ModuleAstLanguage language, const char* path, const char* source,
        size_t length, ModuleAstPrebuildStats* stats) {
    return scope && scope->registry && module_ast_prebuild_imports_impl(&scope->profiles,
        language, path, source, length, stats, scope);
}

bool module_ast_prebuild_scope_await(ModuleAstPrebuildScope* scope,
        const ModuleAstPrebuildProfile* profile, const char* path) {
    return scope && scope->registry && profile && profile->language < MODULE_AST_LANGUAGE_COUNT &&
        scope->profiles.profiles[profile->language] == profile && module_ast_prebuild_await_impl(profile, path, scope);
}

void module_ast_prebuild_scope_close(ModuleAstPrebuildScope* scope) {
    if (!scope) return;
    ModuleAstPrebuildRegistry* registry = scope->registry;
    if (registry) {
        pthread_mutex_lock(&registry->mutex);
        scope->closing = true;
        while (scope->jobs) pthread_cond_wait(&scope->idle, &registry->mutex);
        // Consumers have left this Runtime; only its private futures are removed.
        for (int i = 0; i < scope->tasks->length; i++) {
            ModuleAstPrebuildTask* task = (ModuleAstPrebuildTask*)scope->tasks->data[i];
            registry->tasks.erase({task->key, task});
            module_ast_prebuild_free_task(task);
        }
        for (int i = 0; i < registry->scopes->length; i++) {
            if (registry->scopes->data[i] == scope) { arraylist_remove(registry->scopes, i); break; }
        }
        pthread_mutex_unlock(&registry->mutex);
    }
    pthread_cond_destroy(&scope->idle); arraylist_free(scope->tasks); mem_free(scope);
}

void module_ast_prebuild_cleanup(void) {
    ModuleAstPrebuildRegistry* registry = &g_module_ast_prebuild_registry;
    if (!registry->ready) return;

    // Workers may still discover children when normal loading ends; join them
    // before tearing down the futures they publish into the shared registry.
    tp_destroy(registry->pool);
    registry->pool = NULL;

    pthread_mutex_lock(&registry->mutex);
    for (int i = 0; i < registry->scopes->length; i++) {
        ModuleAstPrebuildScope* scope = (ModuleAstPrebuildScope*)registry->scopes->data[i];
        scope->registry = NULL; scope->closing = true;
        arraylist_clear(scope->tasks);
    }
    size_t cursor = 0;
    ModuleAstPrebuildTaskEntry* entry = NULL;
    while (registry->tasks.next(&cursor, &entry)) {
        module_ast_prebuild_free_task(entry->task);
    }
    registry->tasks.destroy();
    arraylist_free(registry->scopes); registry->scopes = NULL;
    memset(registry->profiles, 0, sizeof(registry->profiles));
    registry->next_visit_mark = 0;
    registry->ready = false;
    pthread_mutex_unlock(&registry->mutex);
    pthread_mutex_destroy(&registry->mutex);
}
