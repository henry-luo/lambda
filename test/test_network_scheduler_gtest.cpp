// test_network_scheduler_gtest.cpp
// Unit tests for the Radiant network scheduler and curl-multi backend.

#include <gtest/gtest.h>

#include "../lambda/network/curl_multi_backend.h"
#include "../lambda/network/cookie_jar.h"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/network/network_resource_manager.h"
#include "../lambda/network/network_scheduler.h"
#include "../lambda/input/css/dom_element.hpp"
#include "../lib/file.h"
#include "../lib/mem.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
// windows mkdir accepts only the path; map the POSIX mode-bearing test call.
#define mkdir(path, mode) _mkdir(path)
#endif

typedef struct CompletionState {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    bool success;
} CompletionState;

static void completion_state_init(CompletionState* state) {
    memset(state, 0, sizeof(CompletionState));
    pthread_mutex_init(&state->mutex, NULL);
    pthread_cond_init(&state->cond, NULL);
}

static void completion_state_destroy(CompletionState* state) {
    pthread_cond_destroy(&state->cond);
    pthread_mutex_destroy(&state->mutex);
}

static void test_download_complete(void* task_data, bool success) {
    NetworkResource* res = (NetworkResource*)task_data;
    CompletionState* state = (CompletionState*)res->user_data;
    pthread_mutex_lock(&state->mutex);
    state->count++;
    state->success = success;
    pthread_cond_broadcast(&state->cond);
    pthread_mutex_unlock(&state->mutex);
}

static void wait_for_completion(CompletionState* state) {
    pthread_mutex_lock(&state->mutex);
    while (state->count == 0) {
        pthread_cond_wait(&state->cond, &state->mutex);
    }
    pthread_mutex_unlock(&state->mutex);
}

static bool write_text_fixture(const char* path, const char* content) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len;
}

static bool read_text_file(const char* path, char* buffer, size_t buffer_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t read_size = fread(buffer, 1, buffer_size - 1, f);
    fclose(f);
    buffer[read_size] = '\0';
    return true;
}

TEST(NetworkResourceCache, AdoptsVerifiedLegacyPrefetchEntry) {
    const char* cache_dir = "./temp/test_cache_legacy_prefetch";
    const char* url = "https://example.com/assets/site.css";
    const char* content = "body { color: rebeccapurple; }";

    ASSERT_TRUE(create_dir(cache_dir));
    char* legacy_path = file_cache_path(url, cache_dir, ".cache");
    ASSERT_NE(legacy_path, nullptr);
    ASSERT_TRUE(file_cache_write_url_entry(legacy_path, url, content, strlen(content)));

    EnhancedFileCache* cache = enhanced_cache_create(cache_dir, 1024 * 1024, 100);
    ASSERT_NE(cache, nullptr);
    char* adopted_path = enhanced_cache_lookup(cache, url);
    ASSERT_NE(adopted_path, nullptr);
    EXPECT_STREQ(adopted_path, legacy_path);
    EXPECT_EQ(enhanced_cache_get_entry_count(cache), 1);

    size_t adopted_size = 0;
    char* adopted_content = read_binary_file(adopted_path, &adopted_size);
    ASSERT_NE(adopted_content, nullptr);
    EXPECT_EQ(adopted_size, strlen(content));
    EXPECT_STREQ(adopted_content, content);

    mem_free(adopted_content);
    mem_free(adopted_path);
    char* key_path = file_cache_key_path(legacy_path);
    ASSERT_NE(key_path, nullptr);
    enhanced_cache_evict_lru(cache);
    EXPECT_FALSE(file_exists(legacy_path));
    EXPECT_FALSE(file_exists(key_path));
    enhanced_cache_destroy(cache);

    if (file_exists(legacy_path)) file_delete(legacy_path);
    if (file_exists(key_path)) file_delete(key_path);
    mem_free(key_path);
    mem_free(legacy_path);
}

TEST(NetworkResourceCache, RejectsMismatchedLegacyPrefetchEntry) {
    const char* cache_dir = "./temp/test_cache_legacy_mismatch";
    const char* requested_url = "https://example.com/assets/requested.css";
    const char* stored_url = "https://example.com/assets/colliding.css";
    const char* content = "body { display: none; }";

    ASSERT_TRUE(create_dir(cache_dir));
    char* legacy_path = file_cache_path(requested_url, cache_dir, ".cache");
    ASSERT_NE(legacy_path, nullptr);
    ASSERT_TRUE(file_cache_write_url_entry(legacy_path, stored_url, content, strlen(content)));

    EnhancedFileCache* cache = enhanced_cache_create(cache_dir, 1024 * 1024, 100);
    ASSERT_NE(cache, nullptr);
    EXPECT_EQ(enhanced_cache_lookup(cache, requested_url), nullptr);
    EXPECT_EQ(enhanced_cache_get_entry_count(cache), 0);
    enhanced_cache_destroy(cache);

    char* key_path = file_cache_key_path(legacy_path);
    ASSERT_NE(key_path, nullptr);
    file_delete(legacy_path);
    file_delete(key_path);
    mem_free(key_path);
    mem_free(legacy_path);
}

TEST(NetworkResourceManager, PrefetchAndTypedConsumerShareOneCachedResource) {
    const char* cache_dir = "./temp/test_cache_manager_prefetch";
    const char* url = "https://example.com/assets/app.js";
    const char* content = "window.parallelLoader = true;";

    ASSERT_TRUE(create_dir(cache_dir));
    EnhancedFileCache* cache = enhanced_cache_create(cache_dir, 1024 * 1024, 100);
    ASSERT_NE(cache, nullptr);
    char* stored_path = enhanced_cache_store(cache, url, content, strlen(content), NULL);
    ASSERT_NE(stored_path, nullptr);
    mem_free(stored_path);

    DomDocument document;
    NetworkResourceManager* manager = resource_manager_create(&document, NULL, cache);
    ASSERT_NE(manager, nullptr);

    NetworkResource* prefetched = resource_manager_prefetch(manager, url, PRIORITY_HIGH);
    ASSERT_NE(prefetched, nullptr);
    NetworkResource* script_consumer = resource_manager_load(
        manager, url, RESOURCE_SCRIPT, PRIORITY_NORMAL, NULL);
    EXPECT_EQ(script_consumer, prefetched);
    EXPECT_EQ(prefetched->type, RESOURCE_PREFETCH);
    EXPECT_TRUE(resource_manager_wait_for_resource(manager, prefetched));

    size_t copied_size = 0;
    char* copied = resource_manager_copy_resource_content(
        manager, url, PRIORITY_HIGH, &copied_size);
    ASSERT_NE(copied, nullptr);
    EXPECT_EQ(copied_size, strlen(content));
    EXPECT_STREQ(copied, content);
    mem_free(copied);

    resource_manager_destroy(manager);
    enhanced_cache_destroy(cache);
}

TEST(NetworkSchedulerCurlMulti, FileUrlCompletesAndWritesLocalResource) {
    mkdir("./temp", 0755);

    const char* fixture_path = "./temp/network_scheduler_file_url.txt";
    const char* fixture_body = "curl multi fixture body\n";
    ASSERT_TRUE(write_text_fixture(fixture_path, fixture_body));

    char cwd[1024];
    ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);

    char source_path[1400];
    snprintf(source_path, sizeof(source_path), "%s/temp/network_scheduler_file_url.txt", cwd);

    char url[1500];
    snprintf(url, sizeof(url), "file://%s", source_path);

    NetworkSchedulerConfig config;
    memset(&config, 0, sizeof(config));
    config.max_global_transfers = 2;
    config.max_transfers_per_origin = 2;
    config.use_curl_multi_backend = true;

    NetworkScheduler* scheduler = network_scheduler_create(NULL, &config);
    ASSERT_NE(scheduler, nullptr);

    CompletionState completion;
    completion_state_init(&completion);

    NetworkResource res;
    memset(&res, 0, sizeof(res));
    res.url = url;
    res.type = RESOURCE_HTML;
    res.state = STATE_DOWNLOADING;
    res.timeout_ms = 5000;
    res.user_data = &completion;
    atomic_store(&res.cancel_requested, false);

    ASSERT_TRUE(network_scheduler_submit_download(scheduler,
                                                  &res,
                                                  test_download_complete,
                                                  res.url,
                                                  PRIORITY_NORMAL));

    network_scheduler_wait_all(scheduler);
    wait_for_completion(&completion);

    EXPECT_TRUE(completion.success);
    ASSERT_NE(res.local_path, nullptr);

    char loaded[128];
    ASSERT_TRUE(read_text_file(res.local_path, loaded, sizeof(loaded)));
    EXPECT_STREQ(loaded, fixture_body);

    if (res.local_path) mem_free(res.local_path);
    if (res.error_message) mem_free(res.error_message);
    completion_state_destroy(&completion);
    network_scheduler_destroy(scheduler);
}

typedef struct BlockingTaskState {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool release_first;
    bool first_started;
    bool first_ran;
    bool second_ran;
} BlockingTaskState;

typedef struct BlockingTask {
    BlockingTaskState* state;
    bool first;
} BlockingTask;

static void blocking_task_fn(void* data) {
    BlockingTask* task = (BlockingTask*)data;
    BlockingTaskState* state = task->state;

    pthread_mutex_lock(&state->mutex);
    if (task->first) {
        state->first_started = true;
        pthread_cond_broadcast(&state->cond);
        while (!state->release_first) {
            pthread_cond_wait(&state->cond, &state->mutex);
        }
        state->first_ran = true;
    } else {
        state->second_ran = true;
    }
    pthread_mutex_unlock(&state->mutex);
}

TEST(NetworkScheduler, CancelsQueuedTaskBehindActiveTransfer) {
    NetworkSchedulerConfig config;
    memset(&config, 0, sizeof(config));
    config.max_global_transfers = 1;
    config.max_transfers_per_origin = 1;
    config.use_curl_multi_backend = false;

    NetworkScheduler* scheduler = network_scheduler_create(NULL, &config);
    ASSERT_NE(scheduler, nullptr);

    BlockingTaskState state;
    memset(&state, 0, sizeof(state));
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.cond, NULL);

    BlockingTask first = { .state = &state, .first = true };
    BlockingTask second = { .state = &state, .first = false };

    ASSERT_TRUE(network_scheduler_submit(scheduler,
                                         blocking_task_fn,
                                         &first,
                                         "https://example.test/first",
                                         PRIORITY_NORMAL));

    pthread_mutex_lock(&state.mutex);
    while (!state.first_started) {
        pthread_cond_wait(&state.cond, &state.mutex);
    }
    pthread_mutex_unlock(&state.mutex);

    ASSERT_TRUE(network_scheduler_submit(scheduler,
                                         blocking_task_fn,
                                         &second,
                                         "https://example.test/second",
                                         PRIORITY_NORMAL));

    EXPECT_EQ(network_scheduler_get_queued_count(scheduler), 1);
    EXPECT_TRUE(network_scheduler_cancel(scheduler, &second));
    EXPECT_EQ(network_scheduler_get_queued_count(scheduler), 0);

    pthread_mutex_lock(&state.mutex);
    state.release_first = true;
    pthread_cond_broadcast(&state.cond);
    pthread_mutex_unlock(&state.mutex);

    network_scheduler_wait_all(scheduler);

    EXPECT_TRUE(state.first_ran);
    EXPECT_FALSE(state.second_ran);

    pthread_cond_destroy(&state.cond);
    pthread_mutex_destroy(&state.mutex);
    network_scheduler_destroy(scheduler);
}
