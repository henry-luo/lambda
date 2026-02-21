//==============================================================================
// MIR Direct Transpiler Tests - Auto-Discovery Based
//
// Runs all Lambda scripts through the direct MIR transpilation path (--mir)
// and compares output against the same expected .txt files used by the C path.
//==============================================================================

#include "test_lambda_helpers.hpp"

//==============================================================================
// Directory Configuration (same directories as test_lambda_gtest.cpp)
//==============================================================================

static const char* FUNCTIONAL_TEST_DIRECTORIES[] = {
    "test/lambda",
    "test/lambda/chart",
};
static const size_t NUM_FUNCTIONAL_TEST_DIRECTORIES = sizeof(FUNCTIONAL_TEST_DIRECTORIES) / sizeof(FUNCTIONAL_TEST_DIRECTORIES[0]);

static const char* PROCEDURAL_TEST_DIRECTORIES[] = {
    "test/lambda/proc",
};
static const size_t NUM_PROCEDURAL_TEST_DIRECTORIES = sizeof(PROCEDURAL_TEST_DIRECTORIES) / sizeof(PROCEDURAL_TEST_DIRECTORIES[0]);

//==============================================================================
// Test Discovery
//==============================================================================

std::vector<LambdaTestInfo> discover_all_mir_tests() {
    std::vector<LambdaTestInfo> all_tests;

    for (size_t i = 0; i < NUM_FUNCTIONAL_TEST_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(FUNCTIONAL_TEST_DIRECTORIES[i], false);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }

    for (size_t i = 0; i < NUM_PROCEDURAL_TEST_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(PROCEDURAL_TEST_DIRECTORIES[i], true);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }

    return all_tests;
}

static std::vector<LambdaTestInfo> g_mir_tests;

//==============================================================================
// Parameterized Test Class
//==============================================================================

class MIRScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
};

TEST_P(MIRScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();
    test_lambda_script_against_file(info.script_path.c_str(), info.expected_path.c_str(), info.is_procedural, /*use_mir=*/true);
}

std::string MIRTestNameGenerator(const ::testing::TestParamInfo<LambdaTestInfo>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    AutoDiscovered,
    MIRScriptTest,
    ::testing::ValuesIn(g_mir_tests),
    MIRTestNameGenerator
);

//==============================================================================
// Main
//==============================================================================

int main(int argc, char **argv) {
    g_mir_tests = discover_all_mir_tests();

    printf("Discovered %zu MIR transpiler tests:\n", g_mir_tests.size());
    for (const auto& test : g_mir_tests) {
        printf("  - %s%s\n", test.test_name.c_str(), test.is_procedural ? " (proc)" : "");
    }
    printf("\n");

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
