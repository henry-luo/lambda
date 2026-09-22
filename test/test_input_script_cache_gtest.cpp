#include <gtest/gtest.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "../lib/file.h"
#include "../lambda/input/input-script-cache.h"
#include "../lambda/runtime/module_ast_prebuild.hpp"
#include "../lib/mem.h"
#include "../lib/shell.h"

static int g_cache_test_destroyed_artifacts = 0;

typedef struct CacheSingleFlightThread {
    InputScriptCache* cache;
    InputScriptRequest request;
    InputScriptBuildClaim claim;
    bool artifact_ready;
    pthread_mutex_t mutex;
    pthread_cond_t entered_cond;
    bool entered;
} CacheSingleFlightThread;

static void* cache_single_flight_waiter(void* value) {
    CacheSingleFlightThread* thread = (CacheSingleFlightThread*)value;
    InputCacheScope* scope = input_script_cache_open_scope(thread->cache);
    InputScriptLease* lease = scope
        ? input_script_cache_acquire(scope, &thread->request) : nullptr;
    pthread_mutex_lock(&thread->mutex);
    thread->entered = true;
    pthread_cond_signal(&thread->entered_cond);
    pthread_mutex_unlock(&thread->mutex);
    if (lease) {
        thread->claim = input_script_cache_claim_build(lease,
            INPUT_SCRIPT_BUILD_AST);
        void* artifact = nullptr;
        thread->artifact_ready = thread->claim == INPUT_SCRIPT_BUILD_READY &&
            input_script_cache_get_ast(lease, &artifact) && artifact != nullptr;
    }
    if (scope) input_script_cache_close_scope(scope);
    return nullptr;
}

static void cache_test_destroy_artifact(void* artifact) {
    (void)artifact;
    g_cache_test_destroyed_artifacts++;
}

static void cache_test_restore_env(const char* name, char* previous_value) {
    if (previous_value) {
        shell_setenv(name, previous_value);
        mem_free(previous_value);
    } else {
        shell_unsetenv(name);
    }
}

typedef struct CacheQueuePrebuildState {
    int generation;
    pthread_mutex_t mutex;
    pthread_cond_t root_discovered_cond;
    bool root_discovered;
    int root_resolutions;
    bool slow_discovery_active;
    bool left_built_after_shared;
    bool right_built_after_shared;
    bool dependent_built_while_slow_discovery_active;
    int shared_builds;
} CacheQueuePrebuildState;

static bool cache_queue_prebuild_append_specifier(ArrayList* specifiers,
        const char* specifier) {
    char* copy = mem_strdup(specifier, MEM_CAT_SYSTEM);
    if (!copy || !arraylist_append(specifiers, copy)) {
        mem_free(copy);
        return false;
    }
    return true;
}

static ArrayList* cache_queue_prebuild_discover(void* opaque,
        const char* source, size_t source_length) {
    (void)source_length;
    CacheQueuePrebuildState* state = (CacheQueuePrebuildState*)opaque;
    ArrayList* specifiers = arraylist_new(2);
    if (!state || !specifiers) return specifiers;
    if (strcmp(source, "root") == 0) {
        if (!cache_queue_prebuild_append_specifier(specifiers, "slow") ||
                !cache_queue_prebuild_append_specifier(specifiers, "left") ||
                !cache_queue_prebuild_append_specifier(specifiers, "right")) {
            for (int index = 0; index < specifiers->length; index++) {
                mem_free(specifiers->data[index]);
            }
            arraylist_free(specifiers);
            return NULL;
        }
    } else if (strcmp(source, "left") == 0 || strcmp(source, "right") == 0) {
        if (!cache_queue_prebuild_append_specifier(specifiers, "shared")) {
            arraylist_free(specifiers);
            return NULL;
        }
    } else if (strcmp(source, "slow") == 0) {
        pthread_mutex_lock(&state->mutex);
        state->slow_discovery_active = true;
        pthread_mutex_unlock(&state->mutex);
        usleep(200000);
        pthread_mutex_lock(&state->mutex);
        state->slow_discovery_active = false;
        pthread_mutex_unlock(&state->mutex);
    }
    return specifiers;
}

static bool cache_queue_prebuild_resolve(void* opaque, const char* importer_path,
        const char* specifier, ModuleAstResolvedImport* out) {
    CacheQueuePrebuildState* state = (CacheQueuePrebuildState*)opaque;
    if (!state || !specifier || !out) return false;
    char path[160];
    snprintf(path, sizeof(path), "temp/cache_queue_prebuild_%d_%s.ls",
        state->generation, specifier);
    out->path = mem_strdup(path, MEM_CAT_SYSTEM);
    out->language = MODULE_AST_LANGUAGE_LAMBDA;
    if (importer_path && strstr(importer_path, "_root.ls")) {
        pthread_mutex_lock(&state->mutex);
        state->root_discovered = ++state->root_resolutions == 3;
        if (state->root_discovered) {
            pthread_cond_broadcast(&state->root_discovered_cond);
        }
        pthread_mutex_unlock(&state->mutex);
    }
    return out->path != NULL;
}

static bool cache_queue_prebuild_build(void* opaque, const char* path) {
    CacheQueuePrebuildState* state = (CacheQueuePrebuildState*)opaque;
    if (!state || !path) return false;
    if (strstr(path, "_shared.ls")) {
        pthread_mutex_lock(&state->mutex);
        state->shared_builds++;
        pthread_mutex_unlock(&state->mutex);
    } else if (strstr(path, "_left.ls") || strstr(path, "_right.ls")) {
        bool is_left = strstr(path, "_left.ls") != NULL;
        pthread_mutex_lock(&state->mutex);
        if (is_left) state->left_built_after_shared = state->shared_builds == 1;
        else state->right_built_after_shared = state->shared_builds == 1;
        state->dependent_built_while_slow_discovery_active =
            state->dependent_built_while_slow_discovery_active ||
            state->slow_discovery_active;
        pthread_mutex_unlock(&state->mutex);
    }
    return true;
}

static InputScriptRequest cache_test_lifecycle_request(const char* identity,
        const char* source) {
    InputScriptRequest request = {};
    request.identity = identity;
    request.source = source;
    request.source_length = strlen(source);
    request.source_kind = INPUT_SCRIPT_SOURCE_INLINE;
    request.language = "lambda";
    request.profile = "lifecycle-test";
    request.parser_abi = "parser-v1";
    request.parse_flags = "default";
    request.resolution_base = "<inline>";
    request.backend = "mir-direct";
    request.execution_mode = "script";
    return request;
}

TEST(InputScriptCacheTest, PrebuildSharedImportClosureCompletesWithoutRegistryDeadlock) {
    ASSERT_EQ(file_ensure_dir("temp"), 0);
    int generation = (int)getpid();
    char root_path[128];
    char left_path[128];
    char right_path[128];
    char shared_path[128];
    char leaf_a_path[128];
    char leaf_b_path[128];
    char root_source[768];
    char left_source[256];
    char right_source[256];
    snprintf(root_path, sizeof(root_path), "temp/cache_prebuild_root_%d.ls", generation);
    snprintf(left_path, sizeof(left_path), "temp/cache_prebuild_left_%d.ls", generation);
    snprintf(right_path, sizeof(right_path), "temp/cache_prebuild_right_%d.ls", generation);
    snprintf(shared_path, sizeof(shared_path), "temp/cache_prebuild_shared_%d.ls", generation);
    snprintf(leaf_a_path, sizeof(leaf_a_path), "temp/cache_prebuild_leaf_a_%d.ls", generation);
    snprintf(leaf_b_path, sizeof(leaf_b_path), "temp/cache_prebuild_leaf_b_%d.ls", generation);
    snprintf(root_source, sizeof(root_source),
        "import .cache_prebuild_leaf_a_%d\n"
        "import .cache_prebuild_leaf_b_%d\n"
        "import .cache_prebuild_left_%d\n"
        "import .cache_prebuild_right_%d\n"
        "leaf_a_value + leaf_b_value + left_value + right_value\n",
        generation, generation, generation, generation);
    snprintf(left_source, sizeof(left_source),
        "import .cache_prebuild_shared_%d\n"
        "pub let left_value = shared_value + 1\n", generation);
    snprintf(right_source, sizeof(right_source),
        "import .cache_prebuild_shared_%d\n"
        "pub let right_value = shared_value + 2\n", generation);
    const char shared_source[] = "pub let shared_value = 40\n";
    // These independent leaves make the first system-function-map lookup run
    // in parallel, while left/right preserve the shared-import deadlock case.
    const char leaf_a_source[] = "pub let leaf_a_value = len([0])\n";
    const char leaf_b_source[] = "pub let leaf_b_value = len([0, 0])\n";
    ASSERT_EQ(write_binary_file(root_path, root_source, strlen(root_source)), 0);
    ASSERT_EQ(write_binary_file(left_path, left_source, strlen(left_source)), 0);
    ASSERT_EQ(write_binary_file(right_path, right_source, strlen(right_source)), 0);
    ASSERT_EQ(write_binary_file(shared_path, shared_source, sizeof(shared_source) - 1), 0);
    ASSERT_EQ(write_binary_file(leaf_a_path, leaf_a_source, sizeof(leaf_a_source) - 1), 0);
    ASSERT_EQ(write_binary_file(leaf_b_path, leaf_b_source, sizeof(leaf_b_source) - 1), 0);

    const char* lambda_exe = "./lambda.exe";
    const char* args[] = {lambda_exe, root_path, NULL};
    const ShellEnvEntry env[] = {
        {"LAMBDA_TIER", "auto"},
        {"LAMBDA_MODULE_AST_THREADS", "2"},
        {NULL, NULL},
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 10000;
    options.merge_stderr = true;
    ShellResult result = shell_exec(lambda_exe, args, &options);
    EXPECT_FALSE(result.timed_out);
    EXPECT_EQ(result.exit_code, 0) << (result.stdout_buf ? result.stdout_buf : "");
    EXPECT_NE(strstr(result.stdout_buf ? result.stdout_buf : "", "86"), nullptr);
    shell_result_free(&result);

    unlink(root_path);
    unlink(left_path);
    unlink(right_path);
    unlink(shared_path);
    unlink(leaf_a_path);
    unlink(leaf_b_path);
}

TEST(InputScriptCacheTest, PrebuildQueuesNestedImportsWithoutDepthBarrier) {
    ASSERT_EQ(file_ensure_dir("temp"), 0);
    CacheQueuePrebuildState state = {};
    state.generation = (int)getpid();
    ASSERT_EQ(pthread_mutex_init(&state.mutex, nullptr), 0);
    ASSERT_EQ(pthread_cond_init(&state.root_discovered_cond, nullptr), 0);

    char root_path[160];
    char slow_path[160];
    char left_path[160];
    char right_path[160];
    char shared_path[160];
    snprintf(root_path, sizeof(root_path), "temp/cache_queue_prebuild_%d_root.ls",
        state.generation);
    snprintf(slow_path, sizeof(slow_path), "temp/cache_queue_prebuild_%d_slow.ls",
        state.generation);
    snprintf(left_path, sizeof(left_path), "temp/cache_queue_prebuild_%d_left.ls",
        state.generation);
    snprintf(right_path, sizeof(right_path), "temp/cache_queue_prebuild_%d_right.ls",
        state.generation);
    snprintf(shared_path, sizeof(shared_path), "temp/cache_queue_prebuild_%d_shared.ls",
        state.generation);
    ASSERT_EQ(write_binary_file(root_path, "root", 4), 0);
    ASSERT_EQ(write_binary_file(slow_path, "slow", 4), 0);
    ASSERT_EQ(write_binary_file(left_path, "left", 4), 0);
    ASSERT_EQ(write_binary_file(right_path, "right", 5), 0);
    ASSERT_EQ(write_binary_file(shared_path, "shared", 6), 0);

    ModuleAstPrebuildProfile profile = {
        "queue-test", MODULE_AST_LANGUAGE_LAMBDA,
        cache_queue_prebuild_discover, cache_queue_prebuild_resolve,
        cache_queue_prebuild_build, &state,
    };
    ModuleAstPrebuildProfiles profiles = {};
    profiles.profiles[MODULE_AST_LANGUAGE_LAMBDA] = &profile;
    const char* previous = shell_getenv("LAMBDA_MODULE_AST_THREADS");
    char* previous_threads = previous ? mem_strdup(previous, MEM_CAT_SYSTEM) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_MODULE_AST_THREADS", "2"));

    ModuleAstPrebuildStats stats = {};
    EXPECT_TRUE(module_ast_prebuild_imports(&profiles, MODULE_AST_LANGUAGE_LAMBDA,
        root_path, "root", 4, &stats));

    // Root scheduling returns immediately. Wait only for the direct futures
    // that make the test's temporary profile/context safe to release.
    pthread_mutex_lock(&state.mutex);
    while (!state.root_discovered) {
        pthread_cond_wait(&state.root_discovered_cond, &state.mutex);
    }
    pthread_mutex_unlock(&state.mutex);
    EXPECT_TRUE(module_ast_prebuild_await_import(&profile, left_path));
    EXPECT_TRUE(module_ast_prebuild_await_import(&profile, right_path));
    EXPECT_TRUE(module_ast_prebuild_await_import(&profile, slow_path));

    cache_test_restore_env("LAMBDA_MODULE_AST_THREADS", previous_threads);
    pthread_mutex_lock(&state.mutex);
    EXPECT_EQ(state.shared_builds, 1);
    EXPECT_TRUE(state.left_built_after_shared);
    EXPECT_TRUE(state.right_built_after_shared);
    EXPECT_TRUE(state.dependent_built_while_slow_discovery_active);
    pthread_mutex_unlock(&state.mutex);
    EXPECT_EQ(stats.worker_pool_runs, 1u);

    unlink(root_path);
    unlink(slow_path);
    unlink(left_path);
    unlink(right_path);
    unlink(shared_path);
    pthread_cond_destroy(&state.root_discovered_cond);
    pthread_mutex_destroy(&state.mutex);
}

TEST(InputScriptCacheTest, DisabledCacheKeepsCrossLanguageModuleCodeAlive) {
    ASSERT_EQ(file_ensure_dir("temp"), 0);
    int generation = (int)getpid();
    char manifest_path[128];
    snprintf(manifest_path, sizeof(manifest_path),
        "temp/cache_cross_language_batch_%d.txt", generation);
    ASSERT_EQ(write_binary_file(manifest_path,
        "test/lambda/binary_js_bridge.ls\n", 32), 0);
    const char* lambda_exe = "./lambda.exe";
    const char* args[] = {lambda_exe, "test-batch", "--no-log", "--timeout=10", NULL};
    const ShellEnvEntry env[] = {
        {"LAMBDA_SCRIPT_CACHE", "off"},
        {NULL, NULL},
    };
    ShellOptions options = {};
    options.env = env;
    options.stdin_path = manifest_path;
    options.timeout_ms = 10000;
    options.merge_stderr = true;
    ShellResult result = shell_exec(lambda_exe, args, &options);
    EXPECT_EQ(result.exit_code, 0) << (result.stdout_buf ? result.stdout_buf : "");
    EXPECT_NE(strstr(result.stdout_buf ? result.stdout_buf : "", "BATCH_END 0"), nullptr);
    EXPECT_NE(strstr(result.stdout_buf ? result.stdout_buf : "", "DEADBEEF"), nullptr);
    shell_result_free(&result);
    unlink(manifest_path);
}

TEST(InputScriptCacheTest, ReusesSourceAndKeepsArtifactsByCompilerKey) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    ASSERT_EQ(input_script_cache_policy(cache), INPUT_SCRIPT_CACHE_ALL);

    InputScriptRequest request = {};
    request.identity = "./temp/script-cache-test.ls";
    request.source = "1 + 2";
    request.source_length = 5;
    request.source_kind = INPUT_SCRIPT_SOURCE_FILE;
    request.language = "lambda";
    request.profile = "lambda";
    request.parser_abi = "parser-v1";
    request.parse_flags = "default";
    request.resolution_base = "./temp/";
    request.backend = "mir-direct";
    request.execution_mode = "script";
    request.ast_abi = 7;
    request.compiler_abi = 11;
    request.optimize_level = 2;

    InputCacheScope* first_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(first_scope, nullptr);
    InputScriptLease* first = input_script_cache_acquire(first_scope, &request);
    ASSERT_NE(first, nullptr);
    ScriptInput* first_input = input_script_lease_input(first);
    ASSERT_NE(first_input, nullptr);
    EXPECT_STREQ(input_script_source(first_input), "1 + 2");
    EXPECT_EQ(input_script_source_length(first_input), 5u);
    uint32_t unit_id = input_script_compilation_unit_id(first_input);
    EXPECT_NE(unit_id, 0u);

    int ast_value = 1;
    int mir_value = 2;
    InputScriptArtifactOps ops = {};
    void* artifact = nullptr;
    EXPECT_FALSE(input_script_cache_get_ast(first, &artifact));
    EXPECT_TRUE(input_script_cache_publish_ast(first, &ast_value, &ops));
    EXPECT_TRUE(input_script_cache_get_ast(first, &artifact));
    EXPECT_EQ(artifact, &ast_value);
    EXPECT_FALSE(input_script_cache_get_mir(first, &artifact));
    EXPECT_TRUE(input_script_cache_publish_mir(first, &mir_value, &ops));
    EXPECT_TRUE(input_script_cache_get_mir(first, &artifact));
    EXPECT_EQ(artifact, &mir_value);
    input_script_cache_close_scope(first_scope);

    InputCacheScope* second_scope = input_script_cache_open_scope(cache);
    InputScriptLease* second = input_script_cache_acquire(second_scope, &request);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(input_script_compilation_unit_id(input_script_lease_input(second)), unit_id);
    EXPECT_TRUE(input_script_cache_get_ast(second, &artifact));
    EXPECT_TRUE(input_script_cache_get_mir(second, &artifact));
    input_script_cache_close_scope(second_scope);

    request.compiler_abi = 12;
    InputCacheScope* third_scope = input_script_cache_open_scope(cache);
    InputScriptLease* third = input_script_cache_acquire(third_scope, &request);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(input_script_compilation_unit_id(input_script_lease_input(third)), unit_id);
    EXPECT_TRUE(input_script_cache_get_ast(third, &artifact));
    EXPECT_FALSE(input_script_cache_get_mir(third, &artifact));
    input_script_cache_close_scope(third_scope);

    // A failed artifact-instantiation certificate can retire this logical
    // source generation without inventing a changed byte snapshot.
    EXPECT_EQ(input_script_cache_invalidate_unit(cache, unit_id), 1u);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.retained_entries, 0u);
    EXPECT_EQ(stats.source_misses, 1u);
    EXPECT_EQ(stats.source_hits, 2u);
    EXPECT_EQ(stats.ast_builds, 1u);
    EXPECT_EQ(stats.mir_builds, 1u);
    EXPECT_EQ(stats.retained_source_bytes, 0u);
    input_script_cache_destroy(cache);

    if (previous_policy) {
        shell_setenv("LAMBDA_SCRIPT_CACHE", previous_policy);
        mem_free(previous_policy);
    }
    else shell_unsetenv("LAMBDA_SCRIPT_CACHE");
}

TEST(InputScriptCacheTest, SingleFlightsArtifactBuildAndFailsClosedWhenPoisoned) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest request = {};
    request.identity = "./temp/script-cache-single-flight.ls";
    request.source = "42";
    request.source_length = 2;
    request.source_kind = INPUT_SCRIPT_SOURCE_FILE;
    request.language = "lambda";
    request.profile = "single-flight";
    request.parser_abi = "parser-v1";
    request.parse_flags = "default";
    request.resolution_base = "./temp/";
    request.backend = "mir-direct";
    request.execution_mode = "script";
    request.ast_abi = 1;

    InputCacheScope* owner_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(owner_scope, nullptr);
    InputScriptLease* owner = input_script_cache_acquire(owner_scope, &request);
    ASSERT_NE(owner, nullptr);
    ASSERT_EQ(input_script_cache_claim_build(owner, INPUT_SCRIPT_BUILD_AST),
        INPUT_SCRIPT_BUILD_OWNER);

    CacheSingleFlightThread waiter = {};
    waiter.cache = cache;
    waiter.request = request;
    ASSERT_EQ(pthread_mutex_init(&waiter.mutex, nullptr), 0);
    ASSERT_EQ(pthread_cond_init(&waiter.entered_cond, nullptr), 0);
    pthread_t waiter_thread;
    ASSERT_EQ(pthread_create(&waiter_thread, nullptr, cache_single_flight_waiter,
        &waiter), 0);
    pthread_mutex_lock(&waiter.mutex);
    while (!waiter.entered) pthread_cond_wait(&waiter.entered_cond, &waiter.mutex);
    pthread_mutex_unlock(&waiter.mutex);

    InputScriptCacheStats during = {};
    for (int i = 0; i < 100; i++) {
        input_script_cache_get_stats(cache, &during);
        if (during.single_flight_waits > 0) break;
        usleep(1000);
    }
    EXPECT_GE(during.single_flight_waits, 1u);

    int ast_value = 42;
    InputScriptArtifactOps ops = {};
    ASSERT_TRUE(input_script_cache_publish_ast(owner, &ast_value, &ops));
    input_script_cache_complete_build(owner, INPUT_SCRIPT_BUILD_AST, true, false);
    ASSERT_EQ(pthread_join(waiter_thread, nullptr), 0);
    EXPECT_EQ(waiter.claim, INPUT_SCRIPT_BUILD_READY);
    EXPECT_TRUE(waiter.artifact_ready);
    pthread_cond_destroy(&waiter.entered_cond);
    pthread_mutex_destroy(&waiter.mutex);
    input_script_cache_close_scope(owner_scope);

    InputScriptRequest poison_request = request;
    poison_request.identity = "./temp/script-cache-poisoned.ls";
    poison_request.source = "bad";
    poison_request.source_length = 3;
    InputCacheScope* poison_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(poison_scope, nullptr);
    InputScriptLease* poison_owner = input_script_cache_acquire(poison_scope,
        &poison_request);
    ASSERT_NE(poison_owner, nullptr);
    ASSERT_EQ(input_script_cache_claim_build(poison_owner, INPUT_SCRIPT_BUILD_AST),
        INPUT_SCRIPT_BUILD_OWNER);
    input_script_cache_complete_build(poison_owner, INPUT_SCRIPT_BUILD_AST, false,
        true);
    input_script_cache_close_scope(poison_scope);

    InputCacheScope* poisoned_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(poisoned_scope, nullptr);
    InputScriptLease* poisoned = input_script_cache_acquire(poisoned_scope,
        &poison_request);
    ASSERT_NE(poisoned, nullptr);
    EXPECT_EQ(input_script_cache_claim_build(poisoned, INPUT_SCRIPT_BUILD_AST),
        INPUT_SCRIPT_BUILD_POISONED);
    input_script_cache_close_scope(poisoned_scope);

    InputScriptRequest changed_request = poison_request;
    changed_request.source = "good";
    changed_request.source_length = 4;
    EXPECT_EQ(input_script_cache_invalidate(cache, &changed_request), 1u);
    InputCacheScope* replacement_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(replacement_scope, nullptr);
    InputScriptLease* replacement = input_script_cache_acquire(replacement_scope,
        &changed_request);
    ASSERT_NE(replacement, nullptr);
    EXPECT_EQ(input_script_cache_claim_build(replacement, INPUT_SCRIPT_BUILD_AST),
        INPUT_SCRIPT_BUILD_OWNER);
    input_script_cache_complete_build(replacement, INPUT_SCRIPT_BUILD_AST, false,
        false);
    input_script_cache_close_scope(replacement_scope);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.ast_builds, 1u);
    EXPECT_EQ(stats.poisoned, 1u);
    EXPECT_GE(stats.single_flight_waits, 1u);
    input_script_cache_destroy(cache);

    if (previous_policy) {
        shell_setenv("LAMBDA_SCRIPT_CACHE", previous_policy);
        mem_free(previous_policy);
    }
    else shell_unsetenv("LAMBDA_SCRIPT_CACHE");
}

TEST(InputScriptCacheTest, DefersInvalidationUntilActiveLeaseCloses) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest old_request = {};
    old_request.identity = "./temp/script-cache-invalidation.ls";
    old_request.source = "old";
    old_request.source_length = 3;
    old_request.source_kind = INPUT_SCRIPT_SOURCE_FILE;
    old_request.language = "lambda";
    old_request.profile = "lambda";
    old_request.parser_abi = "parser-v1";
    old_request.parse_flags = "default";
    old_request.resolution_base = "./temp/";
    old_request.backend = "mir-direct";
    old_request.execution_mode = "script";
    old_request.ast_abi = 7;
    old_request.compiler_abi = 11;

    InputCacheScope* old_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(old_scope, nullptr);
    InputScriptLease* old_lease = input_script_cache_acquire(old_scope,
        &old_request);
    ASSERT_NE(old_lease, nullptr);
    int old_ast = 1;
    InputScriptArtifactOps ops = {cache_test_destroy_artifact, nullptr};
    ASSERT_TRUE(input_script_cache_publish_ast(old_lease, &old_ast, &ops));

    InputScriptRequest new_request = old_request;
    new_request.source = "new";
    InputCacheScope* new_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(new_scope, nullptr);
    ASSERT_NE(input_script_cache_acquire(new_scope, &new_request), nullptr);

    g_cache_test_destroyed_artifacts = 0;
    EXPECT_EQ(input_script_cache_invalidate(cache, &new_request), 1u);
    EXPECT_EQ(g_cache_test_destroyed_artifacts, 0);
    EXPECT_STREQ(input_script_source(input_script_lease_input(old_lease)), "old");

    input_script_cache_close_scope(old_scope);
    EXPECT_EQ(g_cache_test_destroyed_artifacts, 1);
    input_script_cache_close_scope(new_scope);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.invalidations, 1u);
    EXPECT_EQ(stats.retained_entries, 1u);
    input_script_cache_destroy(cache);

    if (previous_policy) {
        shell_setenv("LAMBDA_SCRIPT_CACHE", previous_policy);
        mem_free(previous_policy);
    }
    else shell_unsetenv("LAMBDA_SCRIPT_CACHE");
}

TEST(InputScriptCacheTest, AbandonedBuildClaimPoisonsAndWakesWaiters) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest request = cache_test_lifecycle_request("abandoned-claim",
        "42");

    InputCacheScope* owner_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(owner_scope, nullptr);
    InputScriptLease* owner = input_script_cache_acquire(owner_scope, &request);
    ASSERT_NE(owner, nullptr);
    ASSERT_EQ(input_script_cache_claim_build(owner, INPUT_SCRIPT_BUILD_AST),
        INPUT_SCRIPT_BUILD_OWNER);

    CacheSingleFlightThread waiter = {};
    waiter.cache = cache;
    waiter.request = request;
    ASSERT_EQ(pthread_mutex_init(&waiter.mutex, nullptr), 0);
    ASSERT_EQ(pthread_cond_init(&waiter.entered_cond, nullptr), 0);
    pthread_t waiter_thread;
    ASSERT_EQ(pthread_create(&waiter_thread, nullptr, cache_single_flight_waiter,
        &waiter), 0);
    pthread_mutex_lock(&waiter.mutex);
    while (!waiter.entered) pthread_cond_wait(&waiter.entered_cond, &waiter.mutex);
    pthread_mutex_unlock(&waiter.mutex);
    bool waiter_waiting = false;
    for (int attempt = 0; attempt < 50; attempt++) {
        InputScriptCacheStats stats = {};
        input_script_cache_get_stats(cache, &stats);
        if (stats.single_flight_waits > 0) {
            waiter_waiting = true;
            break;
        }
        usleep(1000);
    }
    ASSERT_TRUE(waiter_waiting);

    input_script_cache_close_scope(owner_scope);
    ASSERT_EQ(pthread_join(waiter_thread, nullptr), 0);
    EXPECT_EQ(waiter.claim, INPUT_SCRIPT_BUILD_POISONED);
    pthread_cond_destroy(&waiter.entered_cond);
    pthread_mutex_destroy(&waiter.mutex);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.poisoned, 1u);
    EXPECT_GE(stats.single_flight_waits, 1u);
    input_script_cache_destroy(cache);
    cache_test_restore_env("LAMBDA_SCRIPT_CACHE", previous_policy);
}

TEST(InputScriptCacheTest, EvictsOnlyInactiveEntriesAtConfiguredByteLimit) {
    const char* previous_policy_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_policy_value
        ? mem_strdup(previous_policy_value, MEM_CAT_CACHE_OTHER) : nullptr;
    const char* previous_limit_value = shell_getenv("LAMBDA_SCRIPT_CACHE_MAX_BYTES");
    char* previous_limit = previous_limit_value
        ? mem_strdup(previous_limit_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE_MAX_BYTES", "5"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest first_request = cache_test_lifecycle_request(
        "cache-budget-first", "aaaa");
    InputScriptRequest second_request = cache_test_lifecycle_request(
        "cache-budget-second", "bbbb");

    InputCacheScope* first_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(first_scope, nullptr);
    InputScriptLease* first = input_script_cache_acquire(first_scope,
        &first_request);
    ASSERT_NE(first, nullptr);
    InputCacheScope* second_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(second_scope, nullptr);
    ASSERT_NE(input_script_cache_acquire(second_scope, &second_request), nullptr);

    InputScriptCacheStats before_release = {};
    input_script_cache_get_stats(cache, &before_release);
    EXPECT_EQ(before_release.evictions, 0u);
    EXPECT_EQ(before_release.retained_entries, 2u);
    EXPECT_EQ(before_release.retention_limit_bytes, 5u);
    EXPECT_EQ(before_release.retention_pressure, 1u);

    input_script_cache_close_scope(second_scope);
    InputScriptCacheStats after_second_release = {};
    input_script_cache_get_stats(cache, &after_second_release);
    EXPECT_EQ(after_second_release.evictions, 1u);
    EXPECT_EQ(after_second_release.retained_entries, 1u);
    EXPECT_EQ(after_second_release.retained_source_bytes, 4u);
    EXPECT_STREQ(input_script_source(input_script_lease_input(first)), "aaaa");

    input_script_cache_close_scope(first_scope);
    input_script_cache_destroy(cache);
    cache_test_restore_env("LAMBDA_SCRIPT_CACHE_MAX_BYTES", previous_limit);
    cache_test_restore_env("LAMBDA_SCRIPT_CACHE", previous_policy);
}

TEST(InputScriptCacheTest, RetainsDependencyConeAtConfiguredByteLimit) {
    const char* previous_policy_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_policy_value
        ? mem_strdup(previous_policy_value, MEM_CAT_CACHE_OTHER) : nullptr;
    const char* previous_limit_value = shell_getenv("LAMBDA_SCRIPT_CACHE_MAX_BYTES");
    char* previous_limit = previous_limit_value
        ? mem_strdup(previous_limit_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE_MAX_BYTES", "5"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest importer_request = cache_test_lifecycle_request(
        "cache-budget-importer", "aaaa");
    InputScriptRequest dependency_request = cache_test_lifecycle_request(
        "cache-budget-dependency", "bbbb");
    InputCacheScope* importer_scope = input_script_cache_open_scope(cache);
    InputCacheScope* dependency_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(importer_scope, nullptr);
    ASSERT_NE(dependency_scope, nullptr);
    InputScriptLease* importer = input_script_cache_acquire(importer_scope,
        &importer_request);
    InputScriptLease* dependency = input_script_cache_acquire(dependency_scope,
        &dependency_request);
    ASSERT_NE(importer, nullptr);
    ASSERT_NE(dependency, nullptr);
    ASSERT_TRUE(input_script_cache_record_dependency_by_unit(cache,
        input_script_compilation_unit_id(input_script_lease_input(importer)),
        input_script_compilation_unit_id(input_script_lease_input(dependency))));

    input_script_cache_close_scope(importer_scope);
    input_script_cache_close_scope(dependency_scope);
    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.evictions, 0u);
    EXPECT_EQ(stats.retained_entries, 2u);
    EXPECT_GE(stats.retention_pressure, 1u);
    input_script_cache_destroy(cache);
    cache_test_restore_env("LAMBDA_SCRIPT_CACHE_MAX_BYTES", previous_limit);
    cache_test_restore_env("LAMBDA_SCRIPT_CACHE", previous_policy);
}

TEST(InputScriptCacheTest, RetiresDependencyConeAfterDependencySourceChanges) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest importer_request = {};
    importer_request.identity = "./temp/script-cache-importer.ls";
    importer_request.source = "import dependency";
    importer_request.source_length = strlen(importer_request.source);
    importer_request.source_kind = INPUT_SCRIPT_SOURCE_FILE;
    importer_request.language = "lambda";
    importer_request.profile = "dependency-test";
    importer_request.parser_abi = "parser-v1";
    importer_request.parse_flags = "default";
    importer_request.resolution_base = "./temp/";
    importer_request.backend = "mir-direct";
    importer_request.execution_mode = "script";

    InputScriptRequest dependency_request = importer_request;
    dependency_request.identity = "./temp/script-cache-dependency.ls";
    dependency_request.source = "old";
    dependency_request.source_length = 3;

    InputCacheScope* importer_scope = input_script_cache_open_scope(cache);
    InputCacheScope* dependency_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(importer_scope, nullptr);
    ASSERT_NE(dependency_scope, nullptr);
    InputScriptLease* importer = input_script_cache_acquire(importer_scope,
        &importer_request);
    InputScriptLease* dependency = input_script_cache_acquire(dependency_scope,
        &dependency_request);
    ASSERT_NE(importer, nullptr);
    ASSERT_NE(dependency, nullptr);
    int importer_ast = 1;
    int dependency_ast = 2;
    InputScriptArtifactOps ops = {cache_test_destroy_artifact, nullptr};
    ASSERT_TRUE(input_script_cache_publish_ast(importer, &importer_ast, &ops));
    ASSERT_TRUE(input_script_cache_publish_ast(dependency, &dependency_ast, &ops));
    ASSERT_TRUE(input_script_cache_record_dependency_by_unit(cache,
        input_script_compilation_unit_id(input_script_lease_input(importer)),
        input_script_compilation_unit_id(input_script_lease_input(dependency))));

    InputScriptRequest changed_dependency = dependency_request;
    changed_dependency.source = "new";
    g_cache_test_destroyed_artifacts = 0;
    EXPECT_EQ(input_script_cache_invalidate(cache, &changed_dependency), 2u);
    EXPECT_EQ(g_cache_test_destroyed_artifacts, 0);
    EXPECT_STREQ(input_script_source(input_script_lease_input(importer)),
        "import dependency");
    EXPECT_STREQ(input_script_source(input_script_lease_input(dependency)), "old");

    input_script_cache_close_scope(importer_scope);
    EXPECT_EQ(g_cache_test_destroyed_artifacts, 1);
    input_script_cache_close_scope(dependency_scope);
    EXPECT_EQ(g_cache_test_destroyed_artifacts, 2);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.invalidations, 2u);
    EXPECT_EQ(stats.dependency_invalidations, 1u);
    EXPECT_EQ(stats.retained_entries, 0u);
    input_script_cache_destroy(cache);

    if (previous_policy) {
        shell_setenv("LAMBDA_SCRIPT_CACHE", previous_policy);
        mem_free(previous_policy);
    }
    else shell_unsetenv("LAMBDA_SCRIPT_CACHE");
}

TEST(InputScriptCacheTest, AcquiresExactFileSnapshot) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    const char* path = "test/lambda/import_vars.ls";
    size_t expected_length = 0;
    char* expected_source = read_binary_file(path, &expected_length);
    ASSERT_NE(expected_source, nullptr);

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest request = {};
    request.identity = path;
    request.source_kind = INPUT_SCRIPT_SOURCE_FILE;
    request.language = "lambda";
    request.profile = "lambda-file";
    request.parser_abi = "parser-v1";
    request.parse_flags = "default";
    request.resolution_base = "test/lambda/";
    request.backend = "mir-direct";
    request.execution_mode = "script";
    request.ast_abi = 7;
    request.compiler_abi = 11;

    InputCacheScope* first_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(first_scope, nullptr);
    InputScriptLease* first = input_script_cache_acquire_file(
        first_scope, &request, path);
    ASSERT_NE(first, nullptr);
    ScriptInput* first_input = input_script_lease_input(first);
    ASSERT_NE(first_input, nullptr);
    EXPECT_EQ(input_script_source_length(first_input), expected_length);
    EXPECT_EQ(memcmp(input_script_source(first_input), expected_source,
        expected_length), 0);

    InputCacheScope* second_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(second_scope, nullptr);
    InputScriptLease* second = input_script_cache_acquire_file(
        second_scope, &request, path);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(input_script_source(input_script_lease_input(second)),
        input_script_source(first_input));
    input_script_cache_close_scope(second_scope);
    input_script_cache_close_scope(first_scope);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.source_misses, 1u);
    EXPECT_EQ(stats.source_hits, 1u);
    EXPECT_EQ(stats.retained_source_bytes, expected_length);
    input_script_cache_destroy(cache);
    mem_free(expected_source);

    if (previous_policy) {
        shell_setenv("LAMBDA_SCRIPT_CACHE", previous_policy);
        mem_free(previous_policy);
    }
    else shell_unsetenv("LAMBDA_SCRIPT_CACHE");
}

TEST(InputScriptCacheTest, SeparatesModuleAndScriptSourceKeys) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest request = {};
    request.identity = "module-key-test";
    request.source = "export_value";
    request.source_length = strlen(request.source);
    request.source_kind = INPUT_SCRIPT_SOURCE_INLINE;
    request.language = "javascript";
    request.profile = "module-key-test";
    request.parser_abi = "parser-v1";
    request.parse_flags = "default";
    request.resolution_base = "./temp/";
    request.backend = "mir-direct";
    request.execution_mode = "script";
    request.ast_abi = 1;

    InputCacheScope* scope = input_script_cache_open_scope(cache);
    ASSERT_NE(scope, nullptr);
    InputScriptLease* script = input_script_cache_acquire(scope, &request);
    ASSERT_NE(script, nullptr);
    request.module_mode = true;
    request.execution_mode = "module";
    InputScriptLease* module = input_script_cache_acquire(scope, &request);
    ASSERT_NE(module, nullptr);
    EXPECT_NE(input_script_lease_input(script), input_script_lease_input(module));
    EXPECT_NE(input_script_compilation_unit_id(input_script_lease_input(script)),
        input_script_compilation_unit_id(input_script_lease_input(module)));
    input_script_cache_close_scope(scope);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.source_misses, 2u);
    EXPECT_EQ(stats.source_hits, 0u);
    EXPECT_EQ(stats.retained_entries, 2u);
    input_script_cache_destroy(cache);

    if (previous_policy) {
        shell_setenv("LAMBDA_SCRIPT_CACHE", previous_policy);
        mem_free(previous_policy);
    }
    else shell_unsetenv("LAMBDA_SCRIPT_CACHE");
}

TEST(InputScriptCacheTest, RetiresChangedFileGeneration) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    const char* path = "./temp/script-cache-generation.ls";
    ASSERT_EQ(write_binary_file(path, "old", 3), 0);
    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest request = {};
    request.identity = path;
    request.source_kind = INPUT_SCRIPT_SOURCE_FILE;
    request.language = "lambda";
    request.profile = "generation-test";
    request.parser_abi = "parser-v1";
    request.parse_flags = "default";
    request.resolution_base = "./temp/";
    request.backend = "mir-direct";
    request.execution_mode = "script";

    InputCacheScope* old_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(old_scope, nullptr);
    InputScriptLease* old_lease = input_script_cache_acquire_file(
        old_scope, &request, path);
    ASSERT_NE(old_lease, nullptr);
    EXPECT_STREQ(input_script_source(input_script_lease_input(old_lease)), "old");
    input_script_cache_close_scope(old_scope);

    ASSERT_EQ(write_binary_file(path, "newer", 5), 0);
    InputCacheScope* new_scope = input_script_cache_open_scope(cache);
    ASSERT_NE(new_scope, nullptr);
    InputScriptLease* new_lease = input_script_cache_acquire_file(
        new_scope, &request, path);
    ASSERT_NE(new_lease, nullptr);
    EXPECT_STREQ(input_script_source(input_script_lease_input(new_lease)), "newer");
    input_script_cache_close_scope(new_scope);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.invalidations, 1u);
    EXPECT_EQ(stats.retained_entries, 1u);
    EXPECT_EQ(stats.source_misses, 2u);
    EXPECT_EQ(stats.source_hits, 0u);
    input_script_cache_destroy(cache);
    EXPECT_EQ(file_delete(path), 0);

    if (previous_policy) {
        shell_setenv("LAMBDA_SCRIPT_CACHE", previous_policy);
        mem_free(previous_policy);
    }
    else shell_unsetenv("LAMBDA_SCRIPT_CACHE");
}

TEST(InputScriptCacheTest, CopiesSuppliedUrlSnapshot) {
    const char* previous_value = shell_getenv("LAMBDA_SCRIPT_CACHE");
    char* previous_policy = previous_value
        ? mem_strdup(previous_value, MEM_CAT_CACHE_OTHER) : nullptr;
    ASSERT_TRUE(shell_setenv("LAMBDA_SCRIPT_CACHE", "all"));

    InputScriptCache* cache = input_script_cache_create();
    ASSERT_NE(cache, nullptr);
    InputScriptRequest request = {};
    request.identity = "https://example.test/script.js";
    request.source_kind = INPUT_SCRIPT_SOURCE_URL;
    request.language = "javascript";
    request.profile = "url-test";
    request.parser_abi = "parser-v1";
    request.parse_flags = "classic";
    request.resolution_base = request.identity;
    request.backend = "mir-direct";
    request.execution_mode = "classic";

    size_t first_length = 0;
    char* first = input_script_cache_copy_source(cache, &request,
        "remote-source", 13, &first_length);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first_length, 13u);
    EXPECT_EQ(memcmp(first, "remote-source", first_length), 0);

    size_t second_length = 0;
    char* second = input_script_cache_copy_source(cache, &request,
        "remote-source", 13, &second_length);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second_length, first_length);
    EXPECT_EQ(memcmp(second, first, first_length), 0);
    mem_free(second);
    mem_free(first);

    InputScriptCacheStats stats = {};
    input_script_cache_get_stats(cache, &stats);
    EXPECT_EQ(stats.source_misses, 1u);
    EXPECT_EQ(stats.source_hits, 1u);
    EXPECT_EQ(stats.retained_entries, 1u);
    input_script_cache_destroy(cache);

    if (previous_policy) {
        shell_setenv("LAMBDA_SCRIPT_CACHE", previous_policy);
        mem_free(previous_policy);
    }
    else shell_unsetenv("LAMBDA_SCRIPT_CACHE");
}
