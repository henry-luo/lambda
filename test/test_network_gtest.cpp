// test_network_gtest.cpp
// Unit tests for Radiant network support components

#include <gtest/gtest.h>
#include "../lib/priority_queue.h"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/network/network_thread_pool.h"
#include "../lambda/network/network_resource_manager.h"
#include "../lambda/network/network_downloader.h"
#include <unistd.h>
#include <pthread.h>

// ============================================================================
// Priority Queue Tests
// ============================================================================

TEST(PriorityQueue, CreateAndDestroy) {
    PriorityQueue* pq = priority_queue_create(10);
    ASSERT_NE(pq, nullptr);
    EXPECT_TRUE(priority_queue_is_empty(pq));
    EXPECT_EQ(priority_queue_size(pq), 0);
    priority_queue_destroy(pq);
}

TEST(PriorityQueue, PushAndPop) {
    PriorityQueue* pq = priority_queue_create(10);
    
    int data1 = 100, data2 = 200, data3 = 300;
    
    // Push items with different priorities
    EXPECT_TRUE(priority_queue_push(pq, &data1, 2));  // Normal
    EXPECT_TRUE(priority_queue_push(pq, &data2, 0));  // Critical
    EXPECT_TRUE(priority_queue_push(pq, &data3, 1));  // High
    
    EXPECT_EQ(priority_queue_size(pq), 3);
    
    // Should pop in priority order: 0, 1, 2
    EXPECT_EQ(priority_queue_pop(pq), &data2);  // Critical (0)
    EXPECT_EQ(priority_queue_pop(pq), &data3);  // High (1)
    EXPECT_EQ(priority_queue_pop(pq), &data1);  // Normal (2)
    
    EXPECT_TRUE(priority_queue_is_empty(pq));
    
    priority_queue_destroy(pq);
}

TEST(PriorityQueue, Peek) {
    PriorityQueue* pq = priority_queue_create(10);
    
    int data1 = 100, data2 = 200;
    priority_queue_push(pq, &data1, 5);
    priority_queue_push(pq, &data2, 3);
    
    // Peek should return highest priority without removing
    EXPECT_EQ(priority_queue_peek(pq), &data2);
    EXPECT_EQ(priority_queue_size(pq), 2);
    
    // Pop should remove it
    EXPECT_EQ(priority_queue_pop(pq), &data2);
    EXPECT_EQ(priority_queue_size(pq), 1);
    
    priority_queue_destroy(pq);
}

TEST(PriorityQueue, Clear) {
    PriorityQueue* pq = priority_queue_create(10);
    
    int data = 100;
    priority_queue_push(pq, &data, 1);
    priority_queue_push(pq, &data, 2);
    priority_queue_push(pq, &data, 3);
    
    EXPECT_EQ(priority_queue_size(pq), 3);
    
    priority_queue_clear(pq);
    
    EXPECT_TRUE(priority_queue_is_empty(pq));
    EXPECT_EQ(priority_queue_size(pq), 0);
    
    priority_queue_destroy(pq);
}

// ============================================================================
// Enhanced File Cache Tests
// ============================================================================

TEST(EnhancedFileCache, CreateAndDestroy) {
    EnhancedFileCache* cache = enhanced_cache_create("./temp/test_cache", 1024 * 1024, 100);
    ASSERT_NE(cache, nullptr);
    EXPECT_EQ(enhanced_cache_get_entry_count(cache), 0);
    EXPECT_EQ(enhanced_cache_get_size(cache), 0);
    enhanced_cache_destroy(cache);
}

TEST(EnhancedFileCache, StoreAndLookup) {
    EnhancedFileCache* cache = enhanced_cache_create("./temp/test_cache", 1024 * 1024, 100);
    
    const char* url = "https://example.com/test.css";
    const char* content = "body { color: red; }";
    size_t size = strlen(content);
    
    // Store content
    char* cache_path = enhanced_cache_store(cache, url, content, size, nullptr);
    ASSERT_NE(cache_path, nullptr);
    
    EXPECT_EQ(enhanced_cache_get_entry_count(cache), 1);
    EXPECT_EQ(enhanced_cache_get_size(cache), size);
    
    free(cache_path);
    
    // Lookup should succeed
    char* lookup_path = enhanced_cache_lookup(cache, url);
    ASSERT_NE(lookup_path, nullptr);
    
    // Verify content
    FILE* f = fopen(lookup_path, "rb");
    ASSERT_NE(f, nullptr);
    
    char buffer[256];
    size_t read_size = fread(buffer, 1, sizeof(buffer), f);
    buffer[read_size] = '\0';
    fclose(f);
    
    EXPECT_STREQ(buffer, content);
    
    free(lookup_path);
    enhanced_cache_destroy(cache);
}

TEST(EnhancedFileCache, CacheMiss) {
    EnhancedFileCache* cache = enhanced_cache_create("./temp/test_cache", 1024 * 1024, 100);
    
    char* path = enhanced_cache_lookup(cache, "https://example.com/nonexistent.css");
    EXPECT_EQ(path, nullptr);
    
    enhanced_cache_destroy(cache);
}

TEST(EnhancedFileCache, LRUEviction) {
    // Create cache with small size limit
    EnhancedFileCache* cache = enhanced_cache_create("./temp/test_cache", 100, 10);
    
    const char* content = "x";  // 1 byte
    
    // Fill cache
    for (int i = 0; i < 150; i++) {
        char url[256];
        sprintf(url, "https://example.com/file%d.txt", i);
        enhanced_cache_store(cache, url, content, 1, nullptr);
    }
    
    // Should have evicted entries to stay under size limit
    EXPECT_LE(enhanced_cache_get_size(cache), 100);
    
    enhanced_cache_destroy(cache);
}

TEST(EnhancedFileCache, HitRate) {
    EnhancedFileCache* cache = enhanced_cache_create("./temp/test_cache", 1024 * 1024, 100);
    
    const char* url = "https://example.com/test.css";
    const char* content = "body { color: red; }";
    
    enhanced_cache_store(cache, url, content, strlen(content), nullptr);
    
    // First lookup - hit
    char* path1 = enhanced_cache_lookup(cache, url);
    EXPECT_NE(path1, nullptr);
    free(path1);
    
    // Second lookup - hit
    char* path2 = enhanced_cache_lookup(cache, url);
    EXPECT_NE(path2, nullptr);
    free(path2);
    
    // Miss
    char* path3 = enhanced_cache_lookup(cache, "https://example.com/missing.css");
    EXPECT_EQ(path3, nullptr);
    
    // Hit rate should be 2/3
    float hit_rate = enhanced_cache_get_hit_rate(cache);
    EXPECT_NEAR(hit_rate, 0.666f, 0.01f);
    
    enhanced_cache_destroy(cache);
}

// ============================================================================
// Thread Pool Tests
// ============================================================================

static void simple_task(void* data) {
    int* counter = (int*)data;
    __sync_fetch_and_add(counter, 1);
}

TEST(NetworkThreadPool, CreateAndDestroy) {
    NetworkThreadPool* pool = thread_pool_create(2);
    ASSERT_NE(pool, nullptr);
    thread_pool_destroy(pool);
}

TEST(NetworkThreadPool, ExecuteTask) {
    NetworkThreadPool* pool = thread_pool_create(2);
    
    int counter = 0;
    thread_pool_enqueue(pool, simple_task, &counter, PRIORITY_NORMAL);
    
    // Wait for task to complete
    thread_pool_wait_all(pool);
    
    EXPECT_EQ(counter, 1);
    
    thread_pool_destroy(pool);
}

TEST(NetworkThreadPool, PriorityOrder) {
    NetworkThreadPool* pool = thread_pool_create(1);  // Single thread to enforce order
    
    int execution_order[3] = {0, 0, 0};
    int order_counter = 0;
    
    auto priority_task = [](void* data) {
        int** ptrs = (int**)data;
        int* order_array = ptrs[0];
        int* counter = ptrs[1];
        int task_id = (int)(long)ptrs[2];
        
        int pos = __sync_fetch_and_add(counter, 1);
        order_array[pos] = task_id;
        usleep(10000);  // 10ms delay
    };
    
    // Enqueue tasks with different priorities
    int* data1[3] = {execution_order, &order_counter, (int*)1};
    int* data2[3] = {execution_order, &order_counter, (int*)2};
    int* data3[3] = {execution_order, &order_counter, (int*)3};
    
    thread_pool_enqueue(pool, (TaskFunction)priority_task, data3, PRIORITY_LOW);      // 3
    thread_pool_enqueue(pool, (TaskFunction)priority_task, data1, PRIORITY_CRITICAL); // 1
    thread_pool_enqueue(pool, (TaskFunction)priority_task, data2, PRIORITY_HIGH);     // 2
    
    thread_pool_wait_all(pool);
    
    // Should execute in priority order: 1, 2, 3
    EXPECT_EQ(execution_order[0], 1);
    EXPECT_EQ(execution_order[1], 2);
    EXPECT_EQ(execution_order[2], 3);
    
    thread_pool_destroy(pool);
}

TEST(NetworkThreadPool, MultipleThreads) {
    NetworkThreadPool* pool = thread_pool_create(4);
    
    int counter = 0;
    
    // Enqueue many tasks
    for (int i = 0; i < 100; i++) {
        thread_pool_enqueue(pool, simple_task, &counter, PRIORITY_NORMAL);
    }
    
    thread_pool_wait_all(pool);
    
    EXPECT_EQ(counter, 100);
    
    thread_pool_destroy(pool);
}

// ========Downloader Tests
// ============================================================================

TEST(NetworkDownloader, HTTPErrorRetryability) {
    // 4xx errors (client errors) are not retryable
    EXPECT_FALSE(is_http_error_retryable(400));  // Bad Request
    EXPECT_FALSE(is_http_error_retryable(404));  // Not Found
    EXPECT_FALSE(is_http_error_retryable(403));  // Forbidden
    
    // 5xx errors (server errors) are retryable
    EXPECT_TRUE(is_http_error_retryable(500));   // Internal Server Error
    EXPECT_TRUE(is_http_error_retryable(503));   // Service Unavailable
    EXPECT_TRUE(is_http_error_retryable(504));   // Gateway Timeout
    
    // 2xx and 3xx are successful
    EXPECT_TRUE(is_http_error_retryable(200));   // OK
    EXPECT_TRUE(is_http_error_retryable(301));   // Moved Permanently
}

TEST(NetworkDownloader, DownloadResourceBasic) {
    // Create a simple network resource for testing
    NetworkResource res = {0};
    res.url = strdup("https://httpbin.org/status/200");
    res.type = RESOURCE_HTML;
    res.state = STATE_PENDING;
    res.timeout_ms = 10000;  // 10 seconds
    res.retry_count = 0;
    res.max_retries = 3;
    
    // Attempt download (this will actually connect to httpbin.org)
    // Skip this test if offline or httpbin is unavailable
    bool success = network_download_resource(&res);
    
    if (success) {
        EXPECT_EQ(res.state, STATE_COMPLETED);
        EXPECT_EQ(res.http_status_code, 200);
        EXPECT_NE(res.end_time, 0.0);
        EXPECT_GT(res.end_time, res.start_time);
    } else {
        // Test is allowed to fail if offline
        GTEST_SKIP() << "Skipping online test - network unavailable or httpbin.org down";
    }
    
    free(res.url);
    free(res.local_path);
    free(res.error_message);
}

TEST(NetworkDownloader, DownloadResourceNotFound) {
    NetworkResource res = {0};
    res.url = strdup("https://httpbin.org/status/404");
    res.type = RESOURCE_HTML;
    res.state = STATE_PENDING;
    res.timeout_ms = 10000;
    res.retry_count = 0;
    res.max_retries = 3;
    
    bool success = network_download_resource(&res);
    
    if (!success && res.http_status_code == 404) {
        EXPECT_EQ(res.state, STATE_FAILED);
        EXPECT_EQ(res.http_status_code, 404);
        EXPECT_NE(res.error_message, nullptr);
        EXPECT_FALSE(is_http_error_retryable(404));  // 404 should not be retryable
    } else if (success) {
        GTEST_SKIP() << "Skipping - httpbin may have changed behavior";
    } else {
        GTEST_SKIP() << "Skipping online test - network unavailable";
    }
    
    free(res.url);
    free(res.local_path);
    free(res.error_message);
}

TEST(NetworkDownloader, DownloadWithTimeoutSettings) {
    NetworkResource res = {0};
    res.url = strdup("https://httpbin.org/delay/2");  // 2 second delay
    res.type = RESOURCE_HTML;
    res.state = STATE_PENDING;
    res.timeout_ms = 5000;  // 5 second timeout (should succeed)
    res.retry_count = 0;
    res.max_retries = 0;  // No retries
    
    bool success = network_download_resource(&res);
    
    if (success) {
        EXPECT_EQ(res.http_status_code, 200);
        // Download should complete within timeout
        double elapsed = (res.end_time - res.start_time) * 1000.0;  // Convert to ms
        EXPECT_LT(elapsed, 5000.0);  // Should be less than timeout
        EXPECT_GT(elapsed, 2000.0);  // Should be at least 2 seconds (delay)
    } else {
        GTEST_SKIP() << "Skipping online test - network unavailable";
    }
    
    free(res.url);
    free(res.local_path);
    free(res.error_message);
}

// ============================================================================
// Network ====================================================================
// Network Resource Manager Tests (Basic)
// ============================================================================

TEST(NetworkResourceManager, CreateAndDestroy) {
    NetworkThreadPool* pool = thread_pool_create(2);
    EnhancedFileCache* cache = enhanced_cache_create("./temp/test_cache", 1024 * 1024, 100);
    
    struct DomDocument* doc = nullptr;  // Placeholder
    NetworkResourceManager* mgr = resource_manager_create(doc, pool, cache);
    
    ASSERT_NE(mgr, nullptr);
    EXPECT_TRUE(resource_manager_is_fully_loaded(mgr));
    EXPECT_EQ(resource_manager_get_pending_count(mgr), 0);
    
    resource_manager_destroy(mgr);
    enhanced_cache_destroy(cache);
    thread_pool_destroy(pool);
}

TEST(NetworkResourceManager, LoadProgress) {
    NetworkThreadPool* pool = thread_pool_create(2);
    EnhancedFileCache* cache = enhanced_cache_create("./temp/test_cache", 1024 * 1024, 100);
    struct DomDocument* doc = nullptr;
    
    NetworkResourceManager* mgr = resource_manager_create(doc, pool, cache);
    
    float progress = resource_manager_get_load_progress(mgr);
    EXPECT_FLOAT_EQ(progress, 1.0f);  // No resources = 100% complete
    
    resource_manager_destroy(mgr);
    enhanced_cache_destroy(cache);
    thread_pool_destroy(pool);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
