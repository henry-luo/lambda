//==============================================================================
// Lambda Extended Script Tests - Auto-Discovery Based
// 
// This file tests Lambda scripts that are known to have issues or require
// extended resources (network access, etc.)
//==============================================================================

#include "test_lambda_helpers.hpp"

//==============================================================================
// Directory Configuration for Extended Tests
//==============================================================================

// Functional extended tests (executed with ./lambda.exe <script>)
static const char* EXTENDED_FUNCTIONAL_DIRECTORIES[] = {
    "test/lambda/ext",
    // Add more extended functional test directories here as needed
};
static const size_t NUM_EXTENDED_FUNCTIONAL_DIRECTORIES = sizeof(EXTENDED_FUNCTIONAL_DIRECTORIES) / sizeof(EXTENDED_FUNCTIONAL_DIRECTORIES[0]);

// Procedural extended tests (executed with ./lambda.exe run <script>)
static const char* EXTENDED_PROCEDURAL_DIRECTORIES[] = {
    "test/lambda/proc-ext",
    // Add more extended procedural test directories here as needed
};
static const size_t NUM_EXTENDED_PROCEDURAL_DIRECTORIES = sizeof(EXTENDED_PROCEDURAL_DIRECTORIES) / sizeof(EXTENDED_PROCEDURAL_DIRECTORIES[0]);

//==============================================================================
// Test Discovery
//==============================================================================

// Discover all extended tests from configured directories
std::vector<LambdaTestInfo> discover_extended_tests() {
    std::vector<LambdaTestInfo> all_tests;
    
    // Discover functional extended script tests
    for (size_t i = 0; i < NUM_EXTENDED_FUNCTIONAL_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(EXTENDED_FUNCTIONAL_DIRECTORIES[i], false);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }
    
    // Discover procedural extended script tests
    for (size_t i = 0; i < NUM_EXTENDED_PROCEDURAL_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(EXTENDED_PROCEDURAL_DIRECTORIES[i], true);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }
    
    return all_tests;
}

// Global test list (populated before main)
static std::vector<LambdaTestInfo> g_extended_tests;

//==============================================================================
// Parameterized Test Class for Extended Lambda Scripts
//==============================================================================

class LambdaExtendedScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
};

TEST_P(LambdaExtendedScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();
    test_lambda_script_against_file(info.script_path.c_str(), info.expected_path.c_str(), info.is_procedural);
}

// Custom name generator for better test output
std::string ExtendedTestNameGenerator(const ::testing::TestParamInfo<LambdaTestInfo>& info) {
    return info.param.test_name;
}

// Register parameterized tests with auto-discovered scripts
INSTANTIATE_TEST_SUITE_P(
    AutoDiscovered,
    LambdaExtendedScriptTest,
    ::testing::ValuesIn([]() -> std::vector<LambdaTestInfo> {
        g_extended_tests = discover_extended_tests();
        return g_extended_tests;
    }()),
    ExtendedTestNameGenerator
);

//==============================================================================
// Main Entry Point
//==============================================================================

int main(int argc, char **argv) {
    // Print discovered tests for visibility
    std::vector<LambdaTestInfo> tests = discover_extended_tests();
    printf("Discovered %zu extended lambda script tests:\n", tests.size());
    for (const auto& test : tests) {
        printf("  - %s%s\n", test.test_name.c_str(), test.is_procedural ? " (proc)" : "");
    }
    printf("\n");
    
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
