#include "module_ast_prebuild.hpp"

#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/shell.h"
#include "../../lib/thread_pool.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct ModuleAstPrebuildNode {
    const ModuleAstPrebuildProfile* profile;
    char* path;
    char* source;
    ArrayList* dependencies;  // intptr_t(index + 1)
    int depth;
    bool discovering;
    bool discovered;
    bool depth_visiting;
} ModuleAstPrebuildNode;

typedef struct ModuleAstPrebuildGraph {
    const ModuleAstPrebuildProfiles* profiles;
    ArrayList* nodes;
    ModuleAstPrebuildStats stats;
} ModuleAstPrebuildGraph;

typedef struct ModuleAstPrebuildJob {
    const ModuleAstPrebuildProfile* profile;
    const char* path;
    bool success;
} ModuleAstPrebuildJob;

static bool module_ast_prebuild_enabled(void) {
    const char* value = shell_getenv("LAMBDA_MODULE_AST_PREBUILD");
    return !value || (strcmp(value, "0") != 0 && strcmp(value, "off") != 0 &&
        strcmp(value, "false") != 0);
}

static int module_ast_prebuild_max_workers(void) {
    const char* value = shell_getenv("LAMBDA_MODULE_AST_THREADS");
    int requested = value ? atoi(value) : 0;
    if (requested < 0) return 0;
    if (requested > 8) return 8;
    return requested;
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
    mem_free(node);
}

static void module_ast_prebuild_free_graph(ModuleAstPrebuildGraph* graph) {
    if (!graph || !graph->nodes) return;
    for (int index = 0; index < graph->nodes->length; index++) {
        module_ast_prebuild_free_node((ModuleAstPrebuildNode*)graph->nodes->data[index]);
    }
    arraylist_free(graph->nodes);
    graph->nodes = NULL;
}

static ModuleAstPrebuildNode* module_ast_prebuild_node_at(
        const ModuleAstPrebuildGraph* graph, int index) {
    return graph && graph->nodes && index >= 0 && index < graph->nodes->length
        ? (ModuleAstPrebuildNode*)graph->nodes->data[index] : NULL;
}

static int module_ast_prebuild_find_node(const ModuleAstPrebuildGraph* graph,
        const ModuleAstPrebuildProfile* profile, const char* path) {
    if (!graph || !graph->nodes || !profile || !path) return -1;
    for (int index = 0; index < graph->nodes->length; index++) {
        ModuleAstPrebuildNode* node = module_ast_prebuild_node_at(graph, index);
        if (node && node->profile == profile && node->path &&
                strcmp(node->path, path) == 0) return index;
    }
    return -1;
}

static int module_ast_prebuild_add_node(ModuleAstPrebuildGraph* graph,
        const ModuleAstPrebuildProfile* profile, char* path, char* source) {
    if (!graph || !profile || !path || !source) {
        mem_free(path);
        mem_free(source);
        return -1;
    }
    ModuleAstPrebuildNode* node = (ModuleAstPrebuildNode*)mem_calloc(1,
        sizeof(ModuleAstPrebuildNode), MEM_CAT_SYSTEM);
    if (!node) {
        mem_free(path);
        mem_free(source);
        return -1;
    }
    node->profile = profile;
    node->path = path;
    node->source = source;
    node->depth = -1;
    if (!arraylist_append(graph->nodes, node)) {
        module_ast_prebuild_free_node(node);
        return -1;
    }
    graph->stats.discovered_modules++;
    return graph->nodes->length - 1;
}

static char* module_ast_prebuild_canonical_path(char* path) {
    if (!path) return NULL;
    char* canonical = file_realpath(path);
    if (!canonical) return path;
    mem_free(path);
    return canonical;
}

static bool module_ast_prebuild_append_dependency(ModuleAstPrebuildNode* parent,
        int dependency_index) {
    if (!parent || dependency_index < 0) return false;
    if (!parent->dependencies) parent->dependencies = arraylist_new(4);
    return parent->dependencies && arraylist_append(parent->dependencies,
        (void*)(intptr_t)(dependency_index + 1));
}

static bool module_ast_prebuild_discover_node(ModuleAstPrebuildGraph* graph,
        int node_index) {
    ModuleAstPrebuildNode* node = module_ast_prebuild_node_at(graph, node_index);
    if (!node || !node->profile || !node->source) return false;
    if (node->discovered || node->discovering) return true;
    node->discovering = true;
    ArrayList* specifiers = node->profile->discover_imports
        ? node->profile->discover_imports(node->profile->context, node->source,
            strlen(node->source)) : NULL;
    for (int spec_index = 0; specifiers && spec_index < specifiers->length;
            spec_index++) {
        const char* specifier = (const char*)specifiers->data[spec_index];
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
        resolved.path = module_ast_prebuild_canonical_path(resolved.path);
        int dependency_index = module_ast_prebuild_find_node(graph,
            dependency_profile, resolved.path);
        if (dependency_index < 0) {
            char* source = read_text_file(resolved.path);
            dependency_index = module_ast_prebuild_add_node(graph,
                dependency_profile, resolved.path, source);
            resolved.path = NULL;
            if (dependency_index < 0) continue;
        }
        mem_free(resolved.path);
        if (!module_ast_prebuild_append_dependency(node, dependency_index)) {
            module_ast_prebuild_free_specs(specifiers);
            node->discovering = false;
            return false;
        }
        if (!module_ast_prebuild_discover_node(graph, dependency_index)) {
            module_ast_prebuild_free_specs(specifiers);
            node->discovering = false;
            return false;
        }
    }
    module_ast_prebuild_free_specs(specifiers);
    node->discovering = false;
    node->discovered = true;
    return true;
}

static int module_ast_prebuild_depth(ModuleAstPrebuildGraph* graph, int node_index) {
    ModuleAstPrebuildNode* node = module_ast_prebuild_node_at(graph, node_index);
    if (!node) return 0;
    if (node->depth >= 0) return node->depth;
    if (node->depth_visiting) return 0;
    node->depth_visiting = true;
    int depth = 0;
    for (int index = 0; node->dependencies && index < node->dependencies->length;
            index++) {
        int dependency_index = (int)(intptr_t)node->dependencies->data[index] - 1;
        int dependency_depth = module_ast_prebuild_depth(graph, dependency_index);
        if (dependency_depth + 1 > depth) depth = dependency_depth + 1;
    }
    node->depth_visiting = false;
    node->depth = depth;
    return depth;
}

static void module_ast_prebuild_job(void* opaque) {
    ModuleAstPrebuildJob* job = (ModuleAstPrebuildJob*)opaque;
    if (!job || !job->profile || !job->profile->build_ast || !job->path) return;
    job->success = job->profile->build_ast(job->profile->context, job->path);
}

static void module_ast_prebuild_run_level(ModuleAstPrebuildGraph* graph,
        int depth) {
    int count = 0;
    for (int index = 1; index < graph->nodes->length; index++) {
        ModuleAstPrebuildNode* node = module_ast_prebuild_node_at(graph, index);
        if (node && node->depth == depth) count++;
    }
    if (count == 0) return;
    ModuleAstPrebuildJob* jobs = (ModuleAstPrebuildJob*)mem_calloc((size_t)count,
        sizeof(ModuleAstPrebuildJob), MEM_CAT_SYSTEM);
    if (!jobs) {
        graph->stats.failed_modules += (uint32_t)count;
        return;
    }
    int next = 0;
    for (int index = 1; index < graph->nodes->length; index++) {
        ModuleAstPrebuildNode* node = module_ast_prebuild_node_at(graph, index);
        if (!node || node->depth != depth) continue;
        jobs[next].profile = node->profile;
        jobs[next].path = node->path;
        next++;
    }
    int workers = module_ast_prebuild_max_workers();
    if (workers == 0 || workers > count) workers = count;
    if (workers > 1) {
        ThreadPool* pool = tp_create_with_stack(workers, 8 * 1024 * 1024);
        if (pool) {
            for (int index = 0; index < count; index++) {
                if (!tp_submit(pool, module_ast_prebuild_job, &jobs[index])) {
                    module_ast_prebuild_job(&jobs[index]);
                }
            }
            tp_wait_all(pool);
            tp_destroy(pool);
            graph->stats.worker_batches++;
        } else {
            for (int index = 0; index < count; index++) {
                module_ast_prebuild_job(&jobs[index]);
            }
        }
    } else {
        for (int index = 0; index < count; index++) {
            module_ast_prebuild_job(&jobs[index]);
        }
    }
    for (int index = 0; index < count; index++) {
        if (jobs[index].success) graph->stats.built_modules++;
        else graph->stats.failed_modules++;
    }
    mem_free(jobs);
}

bool module_ast_prebuild_imports(const ModuleAstPrebuildProfiles* profiles,
        ModuleAstLanguage root_language, const char* root_path,
        const char* root_source, size_t root_source_length,
        ModuleAstPrebuildStats* out_stats) {
    if (out_stats) memset(out_stats, 0, sizeof(*out_stats));
    if (!module_ast_prebuild_enabled() || !profiles ||
            root_language >= MODULE_AST_LANGUAGE_COUNT || !root_path ||
            !profiles->profiles[root_language]) return false;
    char* root_path_copy = mem_strdup(root_path, MEM_CAT_SYSTEM);
    char* root_source_copy = root_source
        ? mem_dup_n(root_source, root_source_length, MEM_CAT_SYSTEM)
        : read_text_file(root_path);
    if (!root_path_copy || !root_source_copy) {
        mem_free(root_path_copy);
        mem_free(root_source_copy);
        return false;
    }
    ModuleAstPrebuildGraph graph = {};
    graph.profiles = profiles;
    graph.nodes = arraylist_new(16);
    if (!graph.nodes) {
        mem_free(root_path_copy);
        mem_free(root_source_copy);
        return false;
    }
    root_path_copy = module_ast_prebuild_canonical_path(root_path_copy);
    int root_index = module_ast_prebuild_add_node(&graph,
        profiles->profiles[root_language], root_path_copy, root_source_copy);
    if (root_index < 0 || !module_ast_prebuild_discover_node(&graph, root_index)) {
        module_ast_prebuild_free_graph(&graph);
        return false;
    }
    int maximum_depth = 0;
    for (int index = 1; index < graph.nodes->length; index++) {
        int depth = module_ast_prebuild_depth(&graph, index);
        if (depth > maximum_depth) maximum_depth = depth;
    }
    for (int depth = 0; depth <= maximum_depth; depth++) {
        module_ast_prebuild_run_level(&graph, depth);
    }
    if (out_stats) *out_stats = graph.stats;
    if (graph.stats.discovered_modules > 1) {
        log_info("module-ast-prebuild: root=%s discovered=%u built=%u failed=%u batches=%u",
            root_path, graph.stats.discovered_modules - 1, graph.stats.built_modules,
            graph.stats.failed_modules, graph.stats.worker_batches);
    }
    module_ast_prebuild_free_graph(&graph);
    return true;
}
