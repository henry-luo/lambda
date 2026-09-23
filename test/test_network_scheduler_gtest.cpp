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
#include "../lib/url.h"

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

TEST(NetworkResourceCache, ReopensPersistedNativeEntry) {
    const char* cache_dir = "./temp/test_cache_native_reopen";
    const char* url = "https://example.com/assets/site.css";
    const char* content = "body { color: rebeccapurple; }";

    ASSERT_TRUE(create_dir(cache_dir));
    EnhancedFileCache* writer = enhanced_cache_create(cache_dir, 1024 * 1024, 100);
    ASSERT_NE(writer, nullptr);
    char* stored_path = enhanced_cache_store(writer, url, content, strlen(content), NULL);
    ASSERT_NE(stored_path, nullptr);
    mem_free(stored_path);
    enhanced_cache_destroy(writer);

    EnhancedFileCache* reader = enhanced_cache_create(cache_dir, 1024 * 1024, 100);
    ASSERT_NE(reader, nullptr);
    char* restored_path = enhanced_cache_lookup(reader, url);
    ASSERT_NE(restored_path, nullptr);
    size_t restored_size = 0;
    char* restored_content = read_binary_file(restored_path, &restored_size);
    ASSERT_NE(restored_content, nullptr);
    EXPECT_EQ(restored_size, strlen(content));
    EXPECT_STREQ(restored_content, content);

    mem_free(restored_content);
    mem_free(restored_path);
    enhanced_cache_clear(reader);
    enhanced_cache_destroy(reader);
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

    DomDocument document = {};
    document.url = url_parse("https://docs.example.test/guide");
    ASSERT_NE(document.url, nullptr);
    NetworkResourceManager* manager = resource_manager_create(&document, NULL, cache);
    ASSERT_NE(manager, nullptr);

    NetworkResource* prefetched = resource_manager_prefetch(manager, url, PRIORITY_HIGH);
    ASSERT_NE(prefetched, nullptr);
    NetworkResource* script_consumer = resource_manager_load(
        manager, url, RESOURCE_SCRIPT, PRIORITY_NORMAL, NULL);
    EXPECT_EQ(script_consumer, prefetched);
    EXPECT_EQ(prefetched->type, RESOURCE_PREFETCH);
    EXPECT_STREQ(prefetched->referrer_url, "https://docs.example.test/guide");
    EXPECT_TRUE(resource_manager_wait_for_resource(manager, prefetched));

    size_t copied_size = 0;
    char* copied = resource_manager_copy_resource_content(
        manager, url, PRIORITY_HIGH, &copied_size);
    ASSERT_NE(copied, nullptr);
    EXPECT_EQ(copied_size, strlen(content));
    EXPECT_STREQ(copied, content);
    mem_free(copied);

    resource_manager_destroy(manager);
    url_destroy(document.url);
    enhanced_cache_destroy(cache);
}

TEST(NetworkResourceManager, PreservesParserBlockingPriorityAcrossConsumers) {
    const char* cache_dir = "./temp/test_cache_manager_priority";
    const char* url = "https://example.com/assets/parser-blocking.js";
    const char* content = "window.parserBlocking = true;";

    ASSERT_TRUE(create_dir(cache_dir));
    EnhancedFileCache* cache = enhanced_cache_create(cache_dir, 1024 * 1024, 100);
    ASSERT_NE(cache, nullptr);
    char* stored_path = enhanced_cache_store(cache, url, content, strlen(content), NULL);
    ASSERT_NE(stored_path, nullptr);
    mem_free(stored_path);

    DomDocument document = {};
    NetworkResourceManager* manager = resource_manager_create(&document, NULL, cache);
    ASSERT_NE(manager, nullptr);

    NetworkResource* parser_blocking = resource_manager_prefetch(
        manager, url, PRIORITY_HIGH);
    ASSERT_NE(parser_blocking, nullptr);
    NetworkResource* later_defer_consumer = resource_manager_prefetch(
        manager, url, PRIORITY_NORMAL);
    EXPECT_EQ(later_defer_consumer, parser_blocking);
    EXPECT_EQ(parser_blocking->priority, PRIORITY_HIGH);

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
