#include "module_ast_prebuild.hpp"

#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/shell.h"
#include "../../lib/thread_pool.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// Static import discovery must stay parser-accurate, but compiling a parent
// before its children would make worker threads wait in load_script(). Keep
// the dependencies as continuations instead: a completed child releases only
// its own parents into the closure-wide pool.

typedef struct ModuleAstPrebuildGraph ModuleAstPrebuildGraph;
typedef struct ModuleAstPrebuildNode ModuleAstPrebuildNode;

typedef enum ModuleAstPrebuildJobKind {
    MODULE_AST_PREBUILD_DISCOVER,
    MODULE_AST_PREBUILD_BUILD,
} ModuleAstPrebuildJobKind;

typedef struct ModuleAstPrebuildJob {
    ModuleAstPrebuildGraph* graph;
    ModuleAstPrebuildNode* node;
    ModuleAstPrebuildJobKind kind;
} ModuleAstPrebuildJob;

struct ModuleAstPrebuildNode {
    const ModuleAstPrebuildProfile* profile;
    char* path;
    char* source;
    ArrayList* dependencies;  // ModuleAstPrebuildNode*
    ArrayList* dependents;    // ModuleAstPrebuildNode*
    uint32_t pending_dependencies;
    bool is_root;
    bool discovery_queued;
    bool discovery_complete;
    bool build_queued;
    bool complete;
    bool failed;
    bool dependency_failed;
};

struct ModuleAstPrebuildGraph {
    const ModuleAstPrebuildProfiles* profiles;
    ArrayList* nodes;
    ThreadPool* pool;
    pthread_mutex_t mutex;
    bool mutex_initialized;
    int pool_workers;
    ModuleAstPrebuildStats stats;
};

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

static void module_ast_prebuild_free_specs(ArrayList* specs) {
    if (!specs) return;
    for (int index = 0; index < specs->length; index++) {
        mem_free(specs->data[index]);
    }
    arraylist_free(specs);
}

static void module_ast_prebuild_free_node(ModuleAstPrebuildNode* node) {
    if (!node) return;
    mem_free(node->path);
    mem_free(node->source);
    arraylist_free(node->dependencies);
    arraylist_free(node->dependents);
    mem_free(node);
}

static void module_ast_prebuild_free_graph(ModuleAstPrebuildGraph* graph) {
    if (!graph) return;
    if (graph->pool) {
        tp_destroy(graph->pool);
        graph->pool = NULL;
    }
    if (graph->nodes) {
        for (int index = 0; index < graph->nodes->length; index++) {
            module_ast_prebuild_free_node(
                (ModuleAstPrebuildNode*)graph->nodes->data[index]);
        }
        arraylist_free(graph->nodes);
        graph->nodes = NULL;
    }
    if (graph->mutex_initialized) {
        pthread_mutex_destroy(&graph->mutex);
        graph->mutex_initialized = false;
    }
}

static char* module_ast_prebuild_canonical_path(char* path) {
    if (!path) return NULL;
    char* canonical = file_realpath(path);
    if (!canonical) return path;
    mem_free(path);
    return canonical;
}

// graph->mutex must be held.
static ModuleAstPrebuildNode* module_ast_prebuild_find_node_locked(
        const ModuleAstPrebuildGraph* graph,
        const ModuleAstPrebuildProfile* profile, const char* path) {
    if (!graph || !graph->nodes || !profile || !path) return NULL;
    for (int index = 0; index < graph->nodes->length; index++) {
        ModuleAstPrebuildNode* node =
            (ModuleAstPrebuildNode*)graph->nodes->data[index];
        if (node && node->profile == profile && node->path &&
                strcmp(node->path, path) == 0) return node;
    }
    return NULL;
}

// graph->mutex must be held. Takes ownership of path and source.
static ModuleAstPrebuildNode* module_ast_prebuild_add_node_locked(
        ModuleAstPrebuildGraph* graph, const ModuleAstPrebuildProfile* profile,
        char* path, char* source, bool is_root) {
    if (!graph || !profile || !path) {
        mem_free(path);
        mem_free(source);
        return NULL;
    }
    ModuleAstPrebuildNode* node = (ModuleAstPrebuildNode*)mem_calloc(1,
        sizeof(ModuleAstPrebuildNode), MEM_CAT_SYSTEM);
    if (!node) {
        mem_free(path);
        mem_free(source);
        return NULL;
    }
    node->profile = profile;
    node->path = path;
    node->source = source;
    node->is_root = is_root;
    if (!arraylist_append(graph->nodes, node)) {
        module_ast_prebuild_free_node(node);
        return NULL;
    }
    graph->stats.discovered_modules++;
    return node;
}

static void module_ast_prebuild_notify_dependents(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node, bool success);
static void module_ast_prebuild_activate_node(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node);

static void module_ast_prebuild_finish_node(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node, bool success) {
    if (!graph || !node) return;
    bool notify = false;
    pthread_mutex_lock(&graph->mutex);
    if (!node->complete) {
        node->complete = true;
        node->failed = !success;
        if (!node->is_root) {
            if (success) graph->stats.built_modules++;
            else graph->stats.failed_modules++;
        }
        notify = true;
    }
    pthread_mutex_unlock(&graph->mutex);
    if (notify) module_ast_prebuild_notify_dependents(graph, node, success);
}

static void module_ast_prebuild_run_job(void* opaque);

static void module_ast_prebuild_queue_job(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node, ModuleAstPrebuildJobKind kind) {
    ModuleAstPrebuildJob* job = (ModuleAstPrebuildJob*)mem_calloc(1,
        sizeof(ModuleAstPrebuildJob), MEM_CAT_SYSTEM);
    if (!job) {
        log_error("module-ast-prebuild: could not queue %s for %s",
            kind == MODULE_AST_PREBUILD_DISCOVER ? "discovery" : "build",
            node && node->path ? node->path : "<unknown>");
        module_ast_prebuild_finish_node(graph, node, false);
        return;
    }
    job->graph = graph;
    job->node = node;
    job->kind = kind;
    if (!graph->pool || !tp_submit(graph->pool, module_ast_prebuild_run_job, job)) {
        // A queue-allocation failure must not strand dependents. Running the
        // isolated job here preserves the ordinary-loader fallback contract.
        module_ast_prebuild_run_job(job);
    }
}

static void module_ast_prebuild_queue_discovery(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node) {
    bool queue = false;
    pthread_mutex_lock(&graph->mutex);
    if (!node->complete && !node->discovery_queued) {
        node->discovery_queued = true;
        queue = true;
    }
    pthread_mutex_unlock(&graph->mutex);
    if (queue) module_ast_prebuild_queue_job(graph, node,
        MODULE_AST_PREBUILD_DISCOVER);
}

static void module_ast_prebuild_queue_build(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node) {
    module_ast_prebuild_queue_job(graph, node, MODULE_AST_PREBUILD_BUILD);
}

// Called after discovery or after a dependency notifies this node.
static void module_ast_prebuild_activate_node(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node) {
    bool queue_build = false;
    bool fail = false;
    pthread_mutex_lock(&graph->mutex);
    if (!node->complete && node->discovery_complete &&
            node->pending_dependencies == 0) {
        if (node->dependency_failed) {
            node->complete = true;
            node->failed = true;
            if (!node->is_root) graph->stats.failed_modules++;
            fail = true;
        } else if (!node->is_root && !node->build_queued) {
            node->build_queued = true;
            queue_build = true;
        }
    }
    pthread_mutex_unlock(&graph->mutex);
    if (fail) module_ast_prebuild_notify_dependents(graph, node, false);
    if (queue_build) module_ast_prebuild_queue_build(graph, node);
}

static void module_ast_prebuild_dependency_finished(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* parent, bool success) {
    if (!graph || !parent) return;
    pthread_mutex_lock(&graph->mutex);
    if (!parent->complete && parent->pending_dependencies > 0) {
        parent->pending_dependencies--;
        if (!success) parent->dependency_failed = true;
    }
    pthread_mutex_unlock(&graph->mutex);
    module_ast_prebuild_activate_node(graph, parent);
}

static void module_ast_prebuild_notify_dependents(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node, bool success) {
    if (!graph || !node) return;
    ModuleAstPrebuildNode** dependents = NULL;
    int count = 0;
    pthread_mutex_lock(&graph->mutex);
    count = node->dependents ? node->dependents->length : 0;
    if (count > 0) {
        dependents = (ModuleAstPrebuildNode**)mem_calloc((size_t)count,
            sizeof(ModuleAstPrebuildNode*), MEM_CAT_SYSTEM);
        if (dependents) {
            for (int index = 0; index < count; index++) {
                dependents[index] =
                    (ModuleAstPrebuildNode*)node->dependents->data[index];
            }
        }
    }
    pthread_mutex_unlock(&graph->mutex);
    if (count > 0 && !dependents) {
        log_error("module-ast-prebuild: could not notify dependents of %s",
            node->path ? node->path : "<unknown>");
        return;
    }
    for (int index = 0; index < count; index++) {
        module_ast_prebuild_dependency_finished(graph, dependents[index], success);
    }
    mem_free(dependents);
}

static ModuleAstPrebuildNode* module_ast_prebuild_request_node(
        ModuleAstPrebuildGraph* graph, const ModuleAstPrebuildProfile* profile,
        char* path) {
    if (!graph || !profile || !path) {
        mem_free(path);
        return NULL;
    }
    path = module_ast_prebuild_canonical_path(path);
    pthread_mutex_lock(&graph->mutex);
    ModuleAstPrebuildNode* node = module_ast_prebuild_find_node_locked(graph,
        profile, path);
    if (!node) node = module_ast_prebuild_add_node_locked(graph, profile, path,
        NULL, false);
    else mem_free(path);
    pthread_mutex_unlock(&graph->mutex);
    if (node) module_ast_prebuild_queue_discovery(graph, node);
    return node;
}

static bool module_ast_prebuild_add_dependency(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* parent, ModuleAstPrebuildNode* child) {
    if (!graph || !parent || !child) return false;
    bool child_complete = false;
    bool child_success = false;
    bool appended = false;
    pthread_mutex_lock(&graph->mutex);
    if (!parent->complete) {
        if (!parent->dependencies) parent->dependencies = arraylist_new(4);
        if (!child->dependents) child->dependents = arraylist_new(4);
        appended = parent->dependencies && child->dependents &&
            arraylist_append(parent->dependencies, child) &&
            arraylist_append(child->dependents, parent);
        if (appended) {
            child_complete = child->complete;
            child_success = !child->failed;
            if (!child_complete) parent->pending_dependencies++;
            else if (!child_success) parent->dependency_failed = true;
        } else {
            parent->dependency_failed = true;
        }
    }
    pthread_mutex_unlock(&graph->mutex);
    if (!appended) {
        log_error("module-ast-prebuild: could not record dependency %s -> %s",
            parent->path ? parent->path : "<unknown>",
            child->path ? child->path : "<unknown>");
        return false;
    }
    if (child_complete) module_ast_prebuild_activate_node(graph, parent);
    return true;
}

static void module_ast_prebuild_discover_node(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node) {
    if (!graph || !node || !node->profile) {
        module_ast_prebuild_finish_node(graph, node, false);
        return;
    }
    if (!node->source) node->source = read_text_file(node->path);
    if (!node->source) {
        log_error("module-ast-prebuild: could not read %s",
            node->path ? node->path : "<unknown>");
        module_ast_prebuild_finish_node(graph, node, false);
        return;
    }
    ArrayList* specifiers = node->profile->discover_imports
        ? node->profile->discover_imports(node->profile->context, node->source,
            strlen(node->source)) : NULL;
    for (int index = 0; specifiers && index < specifiers->length; index++) {
        const char* specifier = (const char*)specifiers->data[index];
        ModuleAstResolvedImport resolved = {};
        if (!specifier || !node->profile->resolve_import ||
                !node->profile->resolve_import(node->profile->context, node->path,
                    specifier, &resolved) || !resolved.path) {
            mem_free(resolved.path);
            continue;
        }
        const ModuleAstPrebuildProfile* dependency_profile =
            resolved.language < MODULE_AST_LANGUAGE_COUNT
            ? graph->profiles->profiles[resolved.language] : NULL;
        if (!dependency_profile) {
            mem_free(resolved.path);
            continue;
        }
        ModuleAstPrebuildNode* child = module_ast_prebuild_request_node(graph,
            dependency_profile, resolved.path);
        if (!child || !module_ast_prebuild_add_dependency(graph, node, child)) {
            pthread_mutex_lock(&graph->mutex);
            node->dependency_failed = true;
            pthread_mutex_unlock(&graph->mutex);
        }
    }
    module_ast_prebuild_free_specs(specifiers);
    pthread_mutex_lock(&graph->mutex);
    if (!node->complete) node->discovery_complete = true;
    pthread_mutex_unlock(&graph->mutex);
    module_ast_prebuild_activate_node(graph, node);
}

static void module_ast_prebuild_build_node(ModuleAstPrebuildGraph* graph,
        ModuleAstPrebuildNode* node) {
    bool success = node && node->profile && node->profile->build_ast && node->path &&
        node->profile->build_ast(node->profile->context, node->path);
    module_ast_prebuild_finish_node(graph, node, success);
}

static void module_ast_prebuild_run_job(void* opaque) {
    ModuleAstPrebuildJob* job = (ModuleAstPrebuildJob*)opaque;
    if (!job) return;
    if (job->kind == MODULE_AST_PREBUILD_DISCOVER) {
        module_ast_prebuild_discover_node(job->graph, job->node);
    } else {
        module_ast_prebuild_build_node(job->graph, job->node);
    }
    mem_free(job);
}

static uint32_t module_ast_prebuild_mark_unresolved(ModuleAstPrebuildGraph* graph) {
    uint32_t unresolved = 0;
    pthread_mutex_lock(&graph->mutex);
    for (int index = 0; graph->nodes && index < graph->nodes->length; index++) {
        ModuleAstPrebuildNode* node =
            (ModuleAstPrebuildNode*)graph->nodes->data[index];
        if (!node || node->is_root || node->complete) continue;
        node->complete = true;
        node->failed = true;
        graph->stats.failed_modules++;
        unresolved++;
    }
    pthread_mutex_unlock(&graph->mutex);
    return unresolved;
}

bool module_ast_prebuild_imports(const ModuleAstPrebuildProfiles* profiles,
        ModuleAstLanguage root_language, const char* root_path,
        const char* root_source, size_t root_source_length,
        ModuleAstPrebuildStats* out_stats) {
    if (out_stats) memset(out_stats, 0, sizeof(*out_stats));
    if (!module_ast_prebuild_enabled() || !profiles ||
            root_language >= MODULE_AST_LANGUAGE_COUNT || !root_path ||
            !profiles->profiles[root_language]) return false;

    ModuleAstPrebuildGraph graph = {};
    graph.profiles = profiles;
    graph.nodes = arraylist_new(16);
    graph.pool_workers = module_ast_prebuild_pool_workers();
    if (!graph.nodes || pthread_mutex_init(&graph.mutex, NULL) != 0) {
        module_ast_prebuild_free_graph(&graph);
        return false;
    }
    graph.mutex_initialized = true;
    graph.pool = tp_create_with_stack(graph.pool_workers, 8 * 1024 * 1024);
    if (graph.pool) graph.stats.worker_pool_runs++;
    else log_warn("module-ast-prebuild: pool creation failed; loading serially");

    char* root_path_copy = module_ast_prebuild_canonical_path(
        mem_strdup(root_path, MEM_CAT_SYSTEM));
    char* root_source_copy = root_source
        ? mem_dup_n(root_source, root_source_length, MEM_CAT_SYSTEM) : NULL;
    pthread_mutex_lock(&graph.mutex);
    ModuleAstPrebuildNode* root = module_ast_prebuild_add_node_locked(&graph,
        profiles->profiles[root_language], root_path_copy, root_source_copy, true);
    pthread_mutex_unlock(&graph.mutex);
    if (!root) {
        module_ast_prebuild_free_graph(&graph);
        return false;
    }

    module_ast_prebuild_queue_discovery(&graph, root);
    if (graph.pool) tp_wait_all(graph.pool);
    uint32_t unresolved = module_ast_prebuild_mark_unresolved(&graph);
    if (unresolved > 0) {
        log_warn("module-ast-prebuild: root=%s skipped=%u unresolved dependency task(s)",
            root_path, unresolved);
    }

    if (out_stats) *out_stats = graph.stats;
    if (graph.stats.discovered_modules > 1) {
        log_info("module-ast-prebuild: root=%s discovered=%u built=%u failed=%u "
            "pool_runs=%u workers=%d",
            root_path, graph.stats.discovered_modules - 1, graph.stats.built_modules,
            graph.stats.failed_modules, graph.stats.worker_pool_runs,
            graph.pool ? tp_thread_count(graph.pool) : 0);
    }
    bool root_ok = !root->failed;
    module_ast_prebuild_free_graph(&graph);
    return root_ok;
}
