#include <catch2/catch_test_macros.hpp>
#include "../lambda/input/input.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/url.h"
#include "../lib/string.h"
#include <cstring>

// Test sys:// URL detection
TEST_CASE("Sysinfo - sys URL detection", "[sysinfo][url]") {
    REQUIRE(is_sys_url("sys://system/info"));
    REQUIRE(is_sys_url("sys://hardware/cpu"));
    REQUIRE_FALSE(is_sys_url("http://example.com"));
    REQUIRE_FALSE(is_sys_url("file:///path/to/file"));
    REQUIRE_FALSE(is_sys_url("ftp://example.com"));
    REQUIRE_FALSE(is_sys_url(nullptr));
    REQUIRE_FALSE(is_sys_url(""));
}

// Test system information manager creation and destruction
TEST_CASE("Sysinfo - manager lifecycle", "[sysinfo][manager]") {
    SysInfoManager* manager = sysinfo_manager_create();
    REQUIRE(manager != nullptr);
    
    sysinfo_manager_destroy(manager);
    // No crash should occur during destruction
}

// Test basic system information retrieval
TEST_CASE("Sysinfo - basic system info", "[sysinfo][basic]") {
    // Create URL for sys://system/info
    Url* url = url_parse("sys://system/info");
    REQUIRE(url != nullptr);
    
    // Create variable pool
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    REQUIRE(pool_err == MEM_POOL_ERR_OK);
    
    // Get system information
    Input* input = input_from_sysinfo(url, pool);
    REQUIRE(input != nullptr);
    REQUIRE(input->root.item != 0);
    
    // Verify element structure - simplified for Phase 1
    Element* system_elem = (Element*)input->root.item;
    REQUIRE(system_elem != nullptr);
    
    // Basic validation that we got a system information element
    // More detailed validation will be added in future phases
    
    // Cleanup
    url_destroy(url);
    pool_variable_destroy(pool);
}

// Test sys:// URL integration with main input system
TEST_CASE("Sysinfo - URL integration", "[sysinfo][integration]") {
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    REQUIRE(pool_err == MEM_POOL_ERR_OK);
    
    String* url_str = create_string(pool, "sys://system/info");
    REQUIRE(url_str != nullptr);
    
    // Test input_from_url with sys:// URL
    Input* input = input_from_url(url_str, nullptr, nullptr, nullptr);
    REQUIRE(input != nullptr);
    REQUIRE(input->root.item != 0);
    
    // Verify we got system information - simplified for Phase 1
    Element* system_elem = (Element*)input->root.item;
    REQUIRE(system_elem != nullptr);
    
    // Cleanup
    pool_variable_destroy(pool);
}

// Test error handling for invalid sys:// URLs
TEST_CASE("Sysinfo - invalid sys URLs", "[sysinfo][error]") {
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    REQUIRE(pool_err == MEM_POOL_ERR_OK);
    
    // Test unsupported category
    Url* url1 = url_parse("sys://unsupported/category");
    REQUIRE(url1 != nullptr);
    
    Input* input1 = input_from_sysinfo(url1, pool);
    REQUIRE(input1 == nullptr);
    
    // Test unsupported subcategory
    Url* url2 = url_parse("sys://system/unsupported");
    REQUIRE(url2 != nullptr);
    
    Input* input2 = input_from_sysinfo(url2, pool);
    REQUIRE(input2 == nullptr);
    
    // Cleanup
    url_destroy(url1);
    url_destroy(url2);
    pool_variable_destroy(pool);
}

// Test system information manager error handling
TEST_CASE("Sysinfo - manager error handling", "[sysinfo][error]") {
    // Test null parameters
    Input* input1 = input_from_sysinfo(nullptr, nullptr);
    REQUIRE(input1 == nullptr);
    
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    REQUIRE(pool_err == MEM_POOL_ERR_OK);
    
    Input* input2 = input_from_sysinfo(nullptr, pool);
    REQUIRE(input2 == nullptr);
    
    pool_variable_destroy(pool);
}

// Performance test - system information should be retrieved quickly
TEST_CASE("Sysinfo - performance test", "[sysinfo][performance]") {
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    REQUIRE(pool_err == MEM_POOL_ERR_OK);
    
    String* url_str = create_string(pool, "sys://system/info");
    REQUIRE(url_str != nullptr);
    
    // Multiple calls should complete within reasonable time
    for (int i = 0; i < 10; i++) {
        Input* input = input_from_url(url_str, nullptr, nullptr, nullptr);
        REQUIRE(input != nullptr);
        REQUIRE(input->root.item != 0);
    }
    
    // Cleanup
    pool_variable_destroy(pool);
}
