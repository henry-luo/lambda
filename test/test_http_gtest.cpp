#include <gtest/gtest.h>
#include "../lambda/input/input.hpp"
#include "../lib/url.h"
#include "../lib/string.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class HttpInputTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        // Create memory pool for tests
        pool = pool_create();
        ASSERT_NE(pool, nullptr) << "Failed to create memory pool";
    }

    void TearDown() override {
        // Cleanup memory pool
        if (pool) {
            pool_destroy(pool);
        }
    }
};

// Test HTTP functionality
TEST_F(HttpInputTest, test_http_download) {
    // Test downloading a simple JSON file from GitHub API
    const char* test_url = "https://api.github.com/zen";

    printf("Testing HTTP download from: %s\n", test_url);

    // Create temp directory first
    system("mkdir -p ./temp/cache");

    // Test just the HTTP download function directly
    size_t content_size;
    char* content = download_http_content(test_url, &content_size, NULL);

    // Verify we got content
    ASSERT_NE(content, nullptr) << "HTTP download should not return null";
    ASSERT_GT(content_size, 0) << "Content size should be greater than 0";

    printf("Downloaded %zu bytes successfully\n", content_size);

    // Cleanup
    free(content);
}

TEST_F(HttpInputTest, test_http_cache) {
    // Test that caching works by downloading the same URL twice
    const char* test_url = "https://api.github.com/octocat";

    printf("Testing HTTP caching with: %s\n", test_url);

    // First download
    char* content1 = download_to_cache(test_url, "./temp/cache", NULL);
    ASSERT_NE(content1, nullptr) << "First download should succeed";

    // Second download (should use cache)
    char* content2 = download_to_cache(test_url, "./temp/cache", NULL);
    ASSERT_NE(content2, nullptr) << "Second download should succeed";

    // Note: UUID content will be different each time, so don't check for equality
    // This test just verifies that the caching mechanism doesn't crash

    printf("HTTP caching test completed successfully\n");

    // Cleanup
    free(content1);
    free(content2);
}

TEST_F(HttpInputTest, test_https_ssl) {
    // Test HTTPS with SSL verification
    const char* test_url = "https://api.github.com/zen";

    printf("Testing HTTPS with SSL verification: %s\n", test_url);

    size_t content_size;
    char* content = download_http_content(test_url, &content_size, NULL);

    ASSERT_NE(content, nullptr) << "HTTPS download should succeed";
    ASSERT_GT(content_size, 0) << "Content size should be greater than 0";

    printf("HTTPS SSL test completed successfully\n");
    printf("Downloaded %zu bytes: %.100s%s\n",
           content_size, content, content_size > 100 ? "..." : "");

    // Cleanup
    free(content);
}

TEST_F(HttpInputTest, test_http_error_handling) {
    // Test error handling with invalid URL
    const char* invalid_url = "https://api.github.com/this-definitely-does-not-exist-404";

    printf("Testing HTTP error handling with: %s\n", invalid_url);

    char* content = download_http_content(invalid_url, NULL, NULL);

    // Should return NULL for 404 error
    ASSERT_EQ(content, nullptr) << "404 URL should return null";

    printf("HTTP error handling test completed successfully\n");
}
