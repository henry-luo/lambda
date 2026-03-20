//==============================================================================
// C2MIR JIT Tests - Auto-Discovery Based
//
// Runs all Lambda scripts through the C2MIR transpilation path (--c2mir)
// and compares output against the same expected .txt files used by MIR Direct.
//==============================================================================

#include "test_lambda_helpers.hpp"

//==============================================================================
// Directory Configuration (same directories as test_lambda_gtest.cpp)
//==============================================================================

// C2MIR tests only run core functional and procedural scripts.
// Chart tests are excluded because they take 20-30s each in C2MIR
// (vs ~2-3s for regular tests), and are already verified via MIR Direct.
static const char* FUNCTIONAL_TEST_DIRECTORIES[] = {
    "test/lambda",
};
static const size_t NUM_FUNCTIONAL_TEST_DIRECTORIES = sizeof(FUNCTIONAL_TEST_DIRECTORIES) / sizeof(FUNCTIONAL_TEST_DIRECTORIES[0]);

static const char* PROCEDURAL_TEST_DIRECTORIES[] = {
    "test/lambda/proc",
};
static const size_t NUM_PROCEDURAL_TEST_DIRECTORIES = sizeof(PROCEDURAL_TEST_DIRECTORIES) / sizeof(PROCEDURAL_TEST_DIRECTORIES[0]);

//==============================================================================
// Test Discovery
//==============================================================================

std::vector<LambdaTestInfo> discover_all_c2mir_tests() {
    std::vector<LambdaTestInfo> all_tests;

    for (size_t i = 0; i < NUM_FUNCTIONAL_TEST_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(FUNCTIONAL_TEST_DIRECTORIES[i], false);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }

    for (size_t i = 0; i < NUM_PROCEDURAL_TEST_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(PROCEDURAL_TEST_DIRECTORIES[i], true);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }

    // filter out slow benchmarks
    std::vector<LambdaTestInfo> filtered;
    for (const auto& test : all_tests) {
        if (!is_slow_benchmark(test.test_name)) {
            filtered.push_back(test);
        }
    }
    return filtered;
}

static std::vector<LambdaTestInfo> g_c2mir_tests;

//==============================================================================
// Parameterized Test Class
//==============================================================================

class C2MIRScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
};

TEST_P(C2MIRScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();
    test_lambda_script_against_file(info.script_path.c_str(), info.expected_path.c_str(), info.is_procedural, /*use_mir=*/false);
}

std::string C2MIRTestNameGenerator(const ::testing::TestParamInfo<LambdaTestInfo>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    AutoDiscovered,
    C2MIRScriptTest,
    ::testing::ValuesIn(g_c2mir_tests),
    C2MIRTestNameGenerator
);

//==============================================================================
// Main
//==============================================================================

int main(int argc, char **argv) {
    g_c2mir_tests = discover_all_c2mir_tests();

    printf("Discovered %zu C2MIR transpiler tests:\n", g_c2mir_tests.size());
    for (const auto& test : g_c2mir_tests) {
        printf("  - %s%s\n", test.test_name.c_str(), test.is_procedural ? " (proc)" : "");
    }
    printf("\n");

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
