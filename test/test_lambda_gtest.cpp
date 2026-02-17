//==============================================================================
// Lambda Script Tests - Auto-Discovery Based
// 
// This file auto-discovers and tests Lambda scripts against expected outputs.
//==============================================================================

#include "test_lambda_helpers.hpp"

//==============================================================================
// Directory Configuration for Baseline Tests
//==============================================================================

// Functional scripts (executed with ./lambda.exe <script>)
static const char* FUNCTIONAL_TEST_DIRECTORIES[] = {
    "test/lambda",
    "test/lambda/chart",
    // Add more functional test directories here as needed
};
static const size_t NUM_FUNCTIONAL_TEST_DIRECTORIES = sizeof(FUNCTIONAL_TEST_DIRECTORIES) / sizeof(FUNCTIONAL_TEST_DIRECTORIES[0]);

// Procedural scripts (executed with ./lambda.exe run <script>)
static const char* PROCEDURAL_TEST_DIRECTORIES[] = {
    "test/lambda/proc",
    // Add more procedural test directories here as needed
};
static const size_t NUM_PROCEDURAL_TEST_DIRECTORIES = sizeof(PROCEDURAL_TEST_DIRECTORIES) / sizeof(PROCEDURAL_TEST_DIRECTORIES[0]);

//==============================================================================
// Test Discovery
//==============================================================================

// Discover all tests from all configured directories
std::vector<LambdaTestInfo> discover_all_tests() {
    std::vector<LambdaTestInfo> all_tests;
    
    // Discover functional script tests
    for (size_t i = 0; i < NUM_FUNCTIONAL_TEST_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(FUNCTIONAL_TEST_DIRECTORIES[i], false);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }
    
    // Discover procedural script tests
    for (size_t i = 0; i < NUM_PROCEDURAL_TEST_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(PROCEDURAL_TEST_DIRECTORIES[i], true);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }
    
    return all_tests;
}

// Global test list (populated before main)
static std::vector<LambdaTestInfo> g_lambda_tests;

//==============================================================================
// Parameterized Test Class for Lambda Scripts
//==============================================================================

class LambdaScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
};

TEST_P(LambdaScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();
    test_lambda_script_against_file(info.script_path.c_str(), info.expected_path.c_str(), info.is_procedural);
}

// Custom name generator for better test output
std::string LambdaTestNameGenerator(const ::testing::TestParamInfo<LambdaTestInfo>& info) {
    return info.param.test_name;
}

// This will be populated in main() before RUN_ALL_TESTS()
INSTANTIATE_TEST_SUITE_P(
    AutoDiscovered,
    LambdaScriptTest,
    ::testing::ValuesIn(g_lambda_tests),
    LambdaTestNameGenerator
);

//==============================================================================
// Negative Tests - verify transpiler reports errors gracefully without crashing
//==============================================================================

// Helper to test that a script reports type errors but doesn't crash
// Note: Lambda currently exits with code 0 even on type errors (errors are reported to stderr)
void test_lambda_script_expects_error(const char* script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe \"%s\" 2>&1", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe \"%s\" 2>&1", script_path);
#endif

    FILE* pipe = popen(command, "r");
    ASSERT_NE(pipe, nullptr) << "Failed to execute command: " << command;

    char buffer[4096];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int exit_code = pclose(pipe);
    (void)exit_code;  // exit code may be 0 even with errors
    
    // Should contain error messages (type_error or [ERR!])
    bool has_error_msg = output.find("type_error") != std::string::npos ||
                         output.find("[ERR!]") != std::string::npos;
    EXPECT_TRUE(has_error_msg) << "Expected error messages in output for: " << script_path
                               << "\nOutput was: " << output;
    
    // Should NOT contain crash indicators
    EXPECT_EQ(output.find("Segmentation fault"), std::string::npos) 
        << "Transpiler crashed on: " << script_path;
    EXPECT_EQ(output.find("SIGABRT"), std::string::npos) 
        << "Transpiler aborted on: " << script_path;
}

TEST(LambdaNegativeTests, test_func_param_type_errors) {
    test_lambda_script_expects_error("test/lambda/negative/func_param_negative.ls");
}

//==============================================================================
// Main - discovers tests before running
//==============================================================================

int main(int argc, char **argv) {
    // Discover all lambda script tests before initializing Google Test
    g_lambda_tests = discover_all_tests();
    
    printf("Discovered %zu lambda script tests:\n", g_lambda_tests.size());
    for (const auto& test : g_lambda_tests) {
        printf("  - %s\n", test.test_name.c_str());
    }
    printf("\n");
    
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
