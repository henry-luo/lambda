// test_network_scheduler_gtest.cpp
// Unit tests for the Radiant network scheduler and curl-multi backend.

#include <gtest/gtest.h>

#include "../lambda/network/curl_multi_backend.h"
#include "../lambda/network/cookie_jar.h"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/network/network_resource_manager.h"
#include "../lambda/network/network_scheduler.h"
#include "../lib/mem.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {

bool network_download_resource(NetworkResource* res) {
    (void)res;
    return false;
}

char* enhanced_cache_try_store(EnhancedFileCache* cache,
                               const char* url,
                               const char* content,
                               size_t size,
                               const HttpCacheHeaders* headers) {
    (void)cache;
    (void)url;
    (void)content;
    (void)size;
    (void)headers;
    return NULL;
}

char* cookie_jar_build_request_header(CookieJar* jar, const char* url, bool is_secure) {
    (void)jar;
    (void)url;
    (void)is_secure;
    return NULL;
}

void cookie_jar_store(CookieJar* jar, const char* request_url, const char* set_cookie_header) {
    (void)jar;
    (void)request_url;
    (void)set_cookie_header;
}

}

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
