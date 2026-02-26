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

// Tests to skip in MIR path (features not yet implemented in MIR transpiler)
static const char* MIR_SKIP_TESTS[] = {
    "object",           // object methods not yet supported in MIR transpiler
    "object_inherit",   // object inheritance not yet supported in MIR transpiler
    "object_default",   // object default values not yet supported in MIR transpiler
    "object_update",    // object update syntax not yet supported in MIR transpiler
    "object_mutation",  // object mutation methods not yet supported in MIR transpiler
    "object_pattern",   // object pattern matching not yet supported in MIR transpiler
    "object_constraint", // object constraint checking not yet supported in MIR transpiler
    "object_direct_access", // object direct struct access (C2MIR only, uses TypeObject features)
    "typed_param_direct_access", // typed param direct access (C2MIR only, includes object types)
};
static const size_t NUM_MIR_SKIP_TESTS = sizeof(MIR_SKIP_TESTS) / sizeof(MIR_SKIP_TESTS[0]);

static bool should_skip_mir_test(const std::string& test_name) {
    for (size_t i = 0; i < NUM_MIR_SKIP_TESTS; i++) {
        if (test_name == MIR_SKIP_TESTS[i]) return true;
    }
    return false;
}

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

    // filter out tests not supported in MIR path
    std::vector<LambdaTestInfo> filtered;
    for (const auto& test : all_tests) {
        if (!should_skip_mir_test(test.test_name)) {
            filtered.push_back(test);
        }
    }
    return filtered;
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
