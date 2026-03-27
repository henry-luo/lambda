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

    // filter out slow benchmarks and tests requiring features not available in C2MIR
    std::vector<LambdaTestInfo> filtered;
    for (const auto& test : all_tests) {
        if (is_slow_benchmark(test.test_name)) continue;
        if (test.test_name == "import_js") continue;  // requires JS runtime (cross-lang import)
        if (test.test_name == "import_js_naming") continue;  // requires JS runtime
        if (test.test_name == "import_py") continue;  // requires Python runtime (cross-lang import)
        if (test.test_name == "view_template") continue;  // view/edit templates only supported in MIR Direct
        if (test.test_name == "view_state") continue;  // view state management only supported in MIR Direct
        if (test.test_name == "render_map") continue;  // render map only supported in MIR Direct
        filtered.push_back(test);
    }
    return filtered;
}

static std::vector<LambdaTestInfo> g_c2mir_tests;

//==============================================================================
// Parameterized Test Class (Batch Mode)
//==============================================================================

class C2MIRScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
public:
    static std::unordered_map<std::string, BatchResult> batch_results;
    static bool batch_executed;

    static void SetUpTestSuite() {
        if (batch_executed) return;

        // Batch ALL scripts regardless of shard index.
        // GTest sharding controls which TEST_P instances run; we just need
        // all results available for lookup.
        std::vector<std::string> scripts;
        std::vector<bool> procs;
        for (const auto& test : g_c2mir_tests) {
            scripts.push_back(test.script_path);
            procs.push_back(test.is_procedural);
        }

        // C2MIR: use_mir=false, batch_chunk_size=5
        batch_results = execute_lambda_batch(scripts, procs, /*use_mir=*/false, /*batch_chunk_size=*/5);
        batch_executed = true;
    }
};

std::unordered_map<std::string, BatchResult> C2MIRScriptTest::batch_results;
bool C2MIRScriptTest::batch_executed = false;

TEST_P(C2MIRScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();

    auto it = batch_results.find(info.script_path);
    ASSERT_TRUE(it != batch_results.end())
        << "Script not found in batch results: " << info.script_path;

    const BatchResult& br = it->second;
    ASSERT_EQ(br.status, 0) << "Script execution failed: " << info.script_path;

    char* actual_output = extract_script_output(br.output);
    ASSERT_NE(actual_output, nullptr) << "Could not extract output for: " << info.script_path;

    trim_trailing_whitespace(actual_output);
    strip_timing_lines(actual_output);
    trim_trailing_whitespace(actual_output);

    char* expected_output = read_expected_output(info.expected_path.c_str());
    ASSERT_NE(expected_output, nullptr) << "Could not read expected file: " << info.expected_path;

    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for script: " << info.script_path
        << " (expected " << strlen(expected_output) << " chars, got " << strlen(actual_output) << " chars)";

    free(expected_output);
    free(actual_output);
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
