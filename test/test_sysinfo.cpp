#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include "../lambda/input/input.h"
#include "../lambda/lambda-data.hpp"
#include "../lib/url.h"
#include "../lib/string.h"
#include <cstring>

// Test suite for system information functionality
TestSuite(sysinfo, .description = "System Information Tests");

// Test sys:// URL detection
Test(sysinfo, test_sys_url_detection) {
    cr_assert(is_sys_url("sys://system/info"), "Should detect sys:// URL");
    cr_assert(is_sys_url("sys://hardware/cpu"), "Should detect sys:// URL with hardware category");
    cr_assert_eq(is_sys_url("http://example.com"), false, "Should not detect HTTP URL as sys://");
    cr_assert_eq(is_sys_url("file:///path/to/file"), false, "Should not detect file:// URL as sys://");
    cr_assert_eq(is_sys_url("ftp://example.com"), false, "Should not detect FTP URL as sys://");
    cr_assert_eq(is_sys_url(nullptr), false, "Should handle null URL gracefully");
    cr_assert_eq(is_sys_url(""), false, "Should handle empty URL gracefully");
}

// Test system information manager creation and destruction
Test(sysinfo, test_sysinfo_manager_lifecycle) {
    SysInfoManager* manager = sysinfo_manager_create();
    cr_assert_not_null(manager, "Should create system information manager");
    
    sysinfo_manager_destroy(manager);
    // No crash should occur during destruction
}

// Test basic system information retrieval
Test(sysinfo, test_system_info_basic) {
    // Create URL for sys://system/info
    Url* url = url_parse("sys://system/info");
    cr_assert_not_null(url, "Should parse sys://system/info URL");
    
    // Create variable pool
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Should create variable pool");
    
    // Get system information
    Input* input = input_from_sysinfo(url, pool);
    cr_assert_not_null(input, "Should create input from sys://system/info");
    cr_assert_neq(input->root.item, 0, "Input should have root element");
    
    // Verify element structure - simplified for Phase 1
    Element* system_elem = (Element*)input->root.item;
    cr_assert_not_null(system_elem, "Root element should not be null");
    
    // Basic validation that we got a system information element
    // More detailed validation will be added in future phases
    
    // Cleanup
    url_destroy(url);
    pool_variable_destroy(pool);
}

// Test sys:// URL integration with main input system
Test(sysinfo, test_sys_url_integration) {
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Should create variable pool");
    
    String* url_str = create_string(pool, "sys://system/info");
    cr_assert_not_null(url_str, "Should create URL string");
    
    // Test input_from_url with sys:// URL
    Input* input = input_from_url(url_str, nullptr, nullptr, nullptr);
    cr_assert_not_null(input, "Should create input from URL");
    cr_assert_neq(input->root.item, 0, "Input should have root element");
    
    // Verify we got system information - simplified for Phase 1
    Element* system_elem = (Element*)input->root.item;
    cr_assert_not_null(system_elem, "Should have system element");
    
    // Cleanup
    pool_variable_destroy(pool);
}

// Test error handling for invalid sys:// URLs
Test(sysinfo, test_invalid_sys_urls) {
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Should create variable pool");
    
    // Test unsupported category
    Url* url1 = url_parse("sys://unsupported/category");
    cr_assert_not_null(url1, "Should parse URL");
    
    Input* input1 = input_from_sysinfo(url1, pool);
    cr_assert_null(input1, "Should return null for unsupported category");
    
    // Test unsupported subcategory
    Url* url2 = url_parse("sys://system/unsupported");
    cr_assert_not_null(url2, "Should parse URL");
    
    Input* input2 = input_from_sysinfo(url2, pool);
    cr_assert_null(input2, "Should return null for unsupported subcategory");
    
    // Cleanup
    url_destroy(url1);
    url_destroy(url2);
    pool_variable_destroy(pool);
}

// Test system information manager error handling
Test(sysinfo, test_sysinfo_manager_error_handling) {
    // Test null parameters
    Input* input1 = input_from_sysinfo(nullptr, nullptr);
    cr_assert_null(input1, "Should handle null URL gracefully");
    
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Should create variable pool");
    
    Input* input2 = input_from_sysinfo(nullptr, pool);
    cr_assert_null(input2, "Should handle null URL with valid pool gracefully");
    
    pool_variable_destroy(pool);
}

// Performance test - system information should be retrieved quickly
Test(sysinfo, test_performance, .timeout = 5.0) {
    VariableMemPool* pool;
    MemPoolError pool_err = pool_variable_init(&pool, 4096, 10);
    cr_assert_eq(pool_err, MEM_POOL_ERR_OK, "Should create variable pool");
    
    String* url_str = create_string(pool, "sys://system/info");
    cr_assert_not_null(url_str, "Should create URL string");
    
    // Multiple calls should complete within timeout
    for (int i = 0; i < 10; i++) {
        Input* input = input_from_url(url_str, nullptr, nullptr, nullptr);
        cr_assert_not_null(input, "Should create input quickly");
        cr_assert_neq(input->root.item, 0, "Should have root element");
    }
    
    // Cleanup
    pool_variable_destroy(pool);
}
