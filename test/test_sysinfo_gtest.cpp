#include <gtest/gtest.h>
#include "../lambda/input/input.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lib/url.h"
#include "../lib/string.h"
#include "../lib/mempool.h"
#include "../lib/log.h"
#include "../lib/test_utils.h"
#include "../lib/file.h"
#include "../lib/shell.h"
#include <unistd.h>
#include <cstring>

// Test fixture for system information functionality
class SysInfoTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override    { pool = tu_setup_pool(); }
    void TearDown() override { tu_teardown_pool(pool); }
};

// Test sys:// URL detection
TEST_F(SysInfoTest, test_sys_url_detection) {
    ASSERT_TRUE(is_sys_url("sys://system/info")) << "Should detect sys:// URL";
    ASSERT_TRUE(is_sys_url("sys://hardware/cpu")) << "Should detect sys:// URL with hardware category";
    ASSERT_FALSE(is_sys_url("http://example.com")) << "Should not detect HTTP URL as sys://";
    ASSERT_FALSE(is_sys_url("file:///path/to/file")) << "Should not detect file:// URL as sys://";
    ASSERT_FALSE(is_sys_url("ftp://example.com")) << "Should not detect FTP URL as sys://";
    ASSERT_FALSE(is_sys_url(nullptr)) << "Should handle null URL gracefully";
    ASSERT_FALSE(is_sys_url("")) << "Should handle empty URL gracefully";
}

// Test system information manager creation and destruction
TEST_F(SysInfoTest, test_sysinfo_manager_lifecycle) {
    SysInfoManager* manager = sysinfo_manager_create();
    ASSERT_NE(manager, nullptr) << "Should create system information manager";

    sysinfo_manager_destroy(manager);
    // No crash should occur during destruction
}

// Test basic system information retrieval
TEST_F(SysInfoTest, test_system_info_basic) {
    // Create URL for sys://system/info
    Url* url = url_parse("sys://system/info");
    ASSERT_NE(url, nullptr) << "Should parse sys://system/info URL";

    // Get system information
    Input* input = input_from_sysinfo(url, pool);
    ASSERT_NE(input, nullptr) << "Should create input from sys://system/info";
    ASSERT_NE(input->root.item, 0) << "Input should have root element";

    // Verify element structure - simplified for Phase 1
    Element* system_elem = (Element*)input->root.item;
    ASSERT_NE(system_elem, nullptr) << "Root element should not be null";

    // Basic validation that we got a system information element
    // More detailed validation will be added in future phases

    // Cleanup
    url_destroy(url);
}

// Test sys:// URL integration with main input system
TEST_F(SysInfoTest, test_sys_url_integration) {
    String* url_str = create_string(pool, "sys://system/info");
    ASSERT_NE(url_str, nullptr) << "Should create URL string";

    // Test input_from_url with sys:// URL
    Input* input = input_from_url(url_str, nullptr, nullptr, nullptr);
    ASSERT_NE(input, nullptr) << "Should create input from URL";
    ASSERT_NE(input->root.item, 0) << "Input should have root element";

    // Verify we got system information - simplified for Phase 1
    Element* system_elem = (Element*)input->root.item;
    ASSERT_NE(system_elem, nullptr) << "Should have system element";
}

// Test error handling for invalid sys:// URLs
TEST_F(SysInfoTest, test_invalid_sys_urls) {
    // Test unsupported category
    Url* url1 = url_parse("sys://unsupported/category");
    ASSERT_NE(url1, nullptr) << "Should parse URL";

    Input* input1 = input_from_sysinfo(url1, pool);
    ASSERT_EQ(input1, nullptr) << "Should return null for unsupported category";

    // Test unsupported subcategory
    Url* url2 = url_parse("sys://system/unsupported");
    ASSERT_NE(url2, nullptr) << "Should parse URL";

    Input* input2 = input_from_sysinfo(url2, pool);
    ASSERT_EQ(input2, nullptr) << "Should return null for unsupported subcategory";

    // Cleanup
    url_destroy(url1);
    url_destroy(url2);
}

// Test system information manager error handling
TEST_F(SysInfoTest, test_sysinfo_manager_error_handling) {
    // Test null parameters
    Input* input1 = input_from_sysinfo(nullptr, nullptr);
    ASSERT_EQ(input1, nullptr) << "Should handle null URL gracefully";

    Input* input2 = input_from_sysinfo(nullptr, pool);
    ASSERT_EQ(input2, nullptr) << "Should handle null URL with valid pool gracefully";
}

// Performance test - system information should be retrieved quickly
TEST_F(SysInfoTest, test_performance) {
    String* url_str = create_string(pool, "sys://system/info");
    ASSERT_NE(url_str, nullptr) << "Should create URL string";

    // Multiple calls should complete within timeout
    for (int i = 0; i < 10; i++) {
        Input* input = input_from_url(url_str, nullptr, nullptr, nullptr);
        ASSERT_NE(input, nullptr) << "Should create input quickly";
        ASSERT_NE(input->root.item, 0) << "Should have root element";
    }
}

// Test invalid sys:// URL handling
TEST_F(SysInfoTest, test_invalid_sys_url) {
    // Test invalid sys URL
    ASSERT_FALSE(is_sys_url("invalid://url")) << "Should not detect invalid URL as sys://";
    ASSERT_FALSE(is_sys_url("sys:/incomplete")) << "Should not detect incomplete sys URL";
}

// The sys.* cache is per thread, but its Input and cached Items live in the
// eval context's pool, which test-batch tears down after every script. Each
// later script must rebuild the cache rather than read the first one's freed
// Items (D4.2.6; vibe/Memory_Context.md §16.4).
static const char* batch_section(const char* out, int index, size_t* len) {
    const char* p = out;
    for (int i = 0; i <= index; i++) {
        p = strstr(p, "BATCH_START");
        if (!p) return nullptr;
        p += strlen("BATCH_START");
    }
    p = strchr(p, '\n');  // skip the script path
    if (!p) return nullptr;
    p++;
    const char* end = strstr(p, "BATCH_END");
    if (!end) return nullptr;
    *len = (size_t)(end - p);
    return p;
}

TEST(SysInfoRuntimeTest, CacheIsRebuiltAfterEachRuntimeInOneProcess) {
    ASSERT_EQ(file_ensure_dir("temp"), 0);
    int generation = (int)getpid();
    char script_path[128];
    char manifest_path[128];
    snprintf(script_path, sizeof(script_path), "temp/sysinfo_batch_%d.ls", generation);
    snprintf(manifest_path, sizeof(manifest_path), "temp/sysinfo_batch_%d.txt", generation);
    // os and cpu are cached for an hour, so later scripts would hit the cache
    const char* script = "[sys.os.name, sys.os.platform, sys.cpu.cores, sys.lambda.version]\n";
    ASSERT_EQ(write_binary_file(script_path, script, strlen(script)), 0);
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s\n%s\n%s\n", script_path, script_path, script_path);
    ASSERT_EQ(write_binary_file(manifest_path, manifest, strlen(manifest)), 0);

    const char* lambda_exe = "./lambda.exe";
    const char* args[] = {lambda_exe, "test-batch", "--no-log", "--timeout=10", NULL};
    ShellOptions options = {};
    options.stdin_path = manifest_path;
    options.timeout_ms = 30000;
    ShellResult result = shell_exec(lambda_exe, args, &options);
    const char* out = result.stdout_buf ? result.stdout_buf : "";
    EXPECT_EQ(result.exit_code, 0) << out;

    size_t first_len = 0;
    const char* first = batch_section(out, 0, &first_len);
    ASSERT_NE(first, nullptr) << out;
    EXPECT_NE(strstr(out, "BATCH_END 0"), nullptr) << out;
    for (int i = 1; i < 3; i++) {
        size_t len = 0;
        const char* section = batch_section(out, i, &len);
        ASSERT_NE(section, nullptr) << "run " << i << ":\n" << out;
        ASSERT_EQ(len, first_len) << "run " << i << ":\n" << out;
        EXPECT_EQ(memcmp(section, first, len), 0) << "run " << i << ":\n" << out;
    }
    shell_result_free(&result);
    unlink(script_path);
    unlink(manifest_path);
}
