//==============================================================================
// Lambda Script Tests - Auto-Discovery Based (MIR Direct path)
// 
// This file auto-discovers and tests Lambda scripts against expected outputs
// using the MIR Direct transpilation path (default JIT).
//==============================================================================

#include "test_lambda_helpers.hpp"

//==============================================================================
// Directory Configuration for Baseline Tests
//==============================================================================

// Functional scripts (executed with ./lambda.exe <script>)
static const char* FUNCTIONAL_TEST_DIRECTORIES[] = {
    "test/lambda",
    "test/lambda/chart",
    "test/lambda/latex",
    "test/lambda/math",
    // Add more functional test directories here as needed
};
static const size_t NUM_FUNCTIONAL_TEST_DIRECTORIES = sizeof(FUNCTIONAL_TEST_DIRECTORIES) / sizeof(FUNCTIONAL_TEST_DIRECTORIES[0]);

// Procedural scripts (executed with ./lambda.exe run <script>)
static const char* PROCEDURAL_TEST_DIRECTORIES[] = {
    "test/lambda/proc",
    "test/benchmark/awfy",
    "test/benchmark/r7rs",
    "test/benchmark/beng",
    "test/benchmark/kostya",
    "test/benchmark/larceny",
    // Add more procedural test directories here as needed
};
static const size_t NUM_PROCEDURAL_TEST_DIRECTORIES = sizeof(PROCEDURAL_TEST_DIRECTORIES) / sizeof(PROCEDURAL_TEST_DIRECTORIES[0]);

//==============================================================================
// MIR Skip List - features not yet implemented in MIR Direct transpiler
//==============================================================================

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
    "map_object_robustness", // comprehensive map/object robustness (uses object features not in MIR)
    // benchmark tests not yet passing in MIR Direct
    "awfy_json",        // JSON benchmark uses features not yet in MIR
    "awfy_json2",       // JSON benchmark uses features not yet in MIR
    "awfy_list2",       // list benchmark uses features not yet in MIR
    "awfy_deltablue2",  // deltablue benchmark produces wrong output in MIR (runs but returns null)
    "beng_fasta",       // fasta benchmark uses features not yet in MIR
    "beng_pidigits",    // pidigits benchmark uses features not yet in MIR
    "beng_revcomp",     // revcomp benchmark uses features not yet in MIR
};
static const size_t NUM_MIR_SKIP_TESTS = sizeof(MIR_SKIP_TESTS) / sizeof(MIR_SKIP_TESTS[0]);

static bool should_skip_mir_test(const std::string& test_name) {
    for (size_t i = 0; i < NUM_MIR_SKIP_TESTS; i++) {
        if (test_name == MIR_SKIP_TESTS[i]) return true;
    }
    return false;
}

//==============================================================================
// C2MIR Skip List - features not yet implemented in legacy C2MIR path
//==============================================================================

static const char* C2MIR_SKIP_TESTS[] = {
    // Sized numeric types (i8, u8, i16, u16, i32, u32, f16, f32, etc.) not in C2MIR parser
    "compact_typed_arrays",
    "sized_numeric_collections",
    "sized_numeric_mixed_expr",
    "sized_numeric_type_annot",
    "typed_array_vector_ops",
    // JS import crashes (segfault in C2MIR when importing .js helpers)
    "import_js",
    "import_js_naming",
    // Fuzzy crash regression uses undefined_var which C2MIR rejects at compile time
    "fuzzy_crash_regression",
    // Features with behavioral differences in C2MIR
    "vector_advanced",      // div-by-zero: C2MIR returns nan, MIR Direct returns 0
    "edit_bridge",          // edit bridge API differs in C2MIR
    "render_map",           // render map output differs in C2MIR
    "view_state",           // view state not fully supported in C2MIR
    "view_template",        // view template not fully supported in C2MIR
    "input_dir",            // directory iteration order differs in C2MIR
    "path",                 // directory iteration order differs in C2MIR
    // C2MIR variadic/float ABI issues with typed arrays (added after Apr 4 binary)
    "awfy_bounce",
    "awfy_bounce2",
    "awfy_deltablue",
    "awfy_deltablue2",
    "awfy_json",
    "awfy_json2",
    "awfy_list",
    "awfy_list2",
    "awfy_permute",
    "awfy_permute2",
    "awfy_queens",
    "awfy_queens2",
    "awfy_richards",
    "awfy_richards2",
    "awfy_sieve",
    "awfy_sieve2",
    "awfy_storage",
    "awfy_storage2",
    "awfy_towers",
    "awfy_towers2",
    "correlation_math",
    "expr",
    "for_clauses_test",
    "for_element_filter",
    "proc_proc_fill",
    "proc_proc_map_set",
    "proc_proc_map_type_change",
    "proc_proc_markup_mutation",
    "proc_proc_param_mutation",
    "proc_proc_param_type_infer",
    "proc_proc_semicolon",
    "proc_proc_typed_array_param",
    "proc_proc_var",
    "proc_proc_var_type_widen",
    "proc_tail_call_proc",
    "proc_test_io_module",
    "proc_test_pipe_file",
    "proc_vmap",
    "proc_while_swap",
    "r7rs_ack2",
    "r7rs_cpstak",
    "r7rs_cpstak2",
    "r7rs_mbrot2",
    "r7rs_nqueens",
    "r7rs_nqueens2",
    "r7rs_sum2",
};
static const size_t NUM_C2MIR_SKIP_TESTS = sizeof(C2MIR_SKIP_TESTS) / sizeof(C2MIR_SKIP_TESTS[0]);

static bool should_skip_c2mir_test(const std::string& test_name) {
    for (size_t i = 0; i < NUM_C2MIR_SKIP_TESTS; i++) {
        if (test_name == C2MIR_SKIP_TESTS[i]) return true;
    }
    return false;
}

//==============================================================================
// Test Discovery
//==============================================================================

// Discover all tests from all configured directories
std::vector<LambdaTestInfo> discover_all_tests() {
    std::vector<LambdaTestInfo> all_tests;
    bool use_c2mir = getenv("LAMBDA_USE_C2MIR") != nullptr;
    
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

    // Filter out unsupported tests and slow benchmark tests
    std::vector<LambdaTestInfo> filtered;
    for (const auto& test : all_tests) {
        if (is_slow_benchmark(test.test_name)) continue;
        if (!use_c2mir && should_skip_mir_test(test.test_name)) continue;
        if (use_c2mir && should_skip_c2mir_test(test.test_name)) continue;
        filtered.push_back(test);
    }
    return filtered;
}

// Global test list (populated before main)
static std::vector<LambdaTestInfo> g_lambda_tests;

//==============================================================================
// Parameterized Test Class for Lambda Scripts (Batch Mode)
//==============================================================================

class LambdaScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
public:
    static std::unordered_map<std::string, BatchResult> batch_results;
    static bool batch_executed;

    static void SetUpTestSuite() {
        if (batch_executed) return;

        // Batch ALL scripts regardless of shard index.
        // GTest sharding controls which TEST_P instances run, but the batch
        // must contain results for all of them because GTest's shard index
        // space (which includes non-parameterized tests like LambdaNegativeTests)
        // differs from g_lambda_tests indices, causing misalignment.
        std::vector<std::string> scripts;
        std::vector<bool> procs;
        for (const auto& test : g_lambda_tests) {
            scripts.push_back(test.script_path);
            procs.push_back(test.is_procedural);
        }

        bool use_mir = !getenv("LAMBDA_USE_C2MIR");
        batch_results = execute_lambda_batch(scripts, procs, use_mir);
        batch_executed = true;
    }
};

std::unordered_map<std::string, BatchResult> LambdaScriptTest::batch_results;
bool LambdaScriptTest::batch_executed = false;

TEST_P(LambdaScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();

    // Look up batch result
    auto it = batch_results.find(info.script_path);
    ASSERT_TRUE(it != batch_results.end())
        << "Script not found in batch results: " << info.script_path;

    const BatchResult& br = it->second;
    ASSERT_EQ(br.status, 0) << "Script execution failed: " << info.script_path;

    // Extract output (handle ##### Script marker)
    char* actual_output = extract_script_output(br.output);
    ASSERT_NE(actual_output, nullptr) << "Could not extract output for: " << info.script_path;

    trim_trailing_whitespace(actual_output);
    strip_timing_lines(actual_output);
    trim_trailing_whitespace(actual_output);

    // Read expected output
    char* expected_output = read_expected_output(info.expected_path.c_str());
    ASSERT_NE(expected_output, nullptr) << "Could not read expected file: " << info.expected_path;

    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for script: " << info.script_path
        << " (expected " << strlen(expected_output) << " chars, got " << strlen(actual_output) << " chars)";

    free(expected_output);
    free(actual_output);
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
    snprintf(command, sizeof(command), "lambda.exe --no-log \"%s\" 2>&1", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe --no-log \"%s\" 2>&1", script_path);
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
    
    // Should contain error messages (type_error, [ERR!], or error[E...])
    bool has_error_msg = output.find("type_error") != std::string::npos ||
                         output.find("[ERR!]") != std::string::npos ||
                         output.find("error[E") != std::string::npos;
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

    // In batch mode (no filter), disable logging for speed.
    // In filtered mode, keep logging enabled for debugging.
    std::string gtest_filter = ::testing::GTEST_FLAG(filter);
    if (gtest_filter == "*") {
#ifdef _WIN32
        _putenv_s("LAMBDA_NO_LOG", "1");
#else
        setenv("LAMBDA_NO_LOG", "1", 1);
#endif
    } else {
#ifdef _WIN32
        _putenv_s("LAMBDA_NO_LOG", "");
#else
        unsetenv("LAMBDA_NO_LOG");
#endif
    }

    return RUN_ALL_TESTS();
}
