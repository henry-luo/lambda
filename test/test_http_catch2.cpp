#include <catch2/catch_test_macros.hpp>
#include "../lambda/input/input.h"
#include "../lib/url.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test HTTP functionality
TEST_CASE("HTTP input - download test", "[http][download]") {
    // Test downloading a simple JSON file from httpbin.org
    const char* test_url = "https://httpbin.org/json";
    
    printf("Testing HTTP download from: %s\n", test_url);
    
    // Create URL string
    String* url_str = (String*)malloc(sizeof(String) + strlen(test_url) + 1);
    url_str->len = strlen(test_url);
    url_str->ref_cnt = 0;
    strcpy(url_str->chars, test_url);
    
    // Create type string for JSON
    String* type_str = (String*)malloc(sizeof(String) + 5);
    type_str->len = 4;
    type_str->ref_cnt = 0;
    strcpy(type_str->chars, "json");
    
    // Test input_from_url with HTTP URL
    Input* input = input_from_url(url_str, type_str, NULL, NULL);
    
    // Verify we got a valid input
    REQUIRE(input != nullptr);
    REQUIRE(input->url != nullptr);
    
    printf("HTTP test completed successfully\n");
    
    // Cleanup
    free(url_str);
    free(type_str);
}

TEST_CASE("HTTP input - caching test", "[http][cache]") {
    // Test that caching works by downloading the same URL twice
    const char* test_url = "https://httpbin.org/uuid";
    
    printf("Testing HTTP caching with: %s\n", test_url);
    
    // First download
    char* content1 = download_to_cache(test_url, "./temp/cache", NULL);
    REQUIRE(content1 != nullptr);
    
    // Second download (should use cache)
    char* content2 = download_to_cache(test_url, "./temp/cache", NULL);
    REQUIRE(content2 != nullptr);
    
    // Content should be the same (cached)
    REQUIRE(strcmp(content1, content2) == 0);
    
    printf("HTTP caching test completed successfully\n");
    
    // Cleanup
    free(content1);
    free(content2);
}

TEST_CASE("HTTP input - HTTPS SSL test", "[http][ssl]") {
    // Test HTTPS with SSL verification
    const char* test_url = "https://api.github.com/zen";
    
    printf("Testing HTTPS with SSL verification: %s\n", test_url);
    
    size_t content_size;
    char* content = download_http_content(test_url, &content_size, NULL);
    
    REQUIRE(content != nullptr);
    REQUIRE(content_size > 0);
    
    printf("HTTPS SSL test completed successfully\n");
    printf("Downloaded %zu bytes: %.100s%s\n", 
           content_size, content, content_size > 100 ? "..." : "");
    
    // Cleanup
    free(content);
}

TEST_CASE("HTTP input - error handling test", "[http][error]") {
    // Test error handling with invalid URL
    const char* invalid_url = "https://httpbin.org/status/404";
    
    printf("Testing HTTP error handling with: %s\n", invalid_url);
    
    char* content = download_http_content(invalid_url, NULL, NULL);
    
    // Should return NULL for 404 error
    REQUIRE(content == nullptr);
    
    printf("HTTP error handling test completed successfully\n");
}
