//==============================================================================
// Ruby Transpiler Tests - Auto-Discovery Based
//
// Auto-discovers test/rb/test_rb_*.rb files that have a matching .txt
// expected-output file and runs them in parallel via `./lambda.exe rb`.
//
// Each script is spawned in its own thread (SetUpTestSuite), results are
// cached in a map, and TEST_P compares actual vs expected output.
//==============================================================================

#include "test_lambda_helpers.hpp"

#include <atomic>
#include <mutex>

//==============================================================================
// Ruby test discovery
//==============================================================================

//==============================================================================
// Skip list — tests that are known broken / not yet passing
//==============================================================================

static const char* RB_SKIP_TESTS[] = {
    // placeholder — add test names here as needed
    "",
};
static const size_t NUM_RB_SKIP_TESTS = sizeof(RB_SKIP_TESTS) / sizeof(RB_SKIP_TESTS[0]);

static bool should_skip_rb_test(const std::string& test_name) {
    for (size_t i = 0; i < NUM_RB_SKIP_TESTS; i++) {
        if (test_name == RB_SKIP_TESTS[i]) return true;
    }
    return false;
}

// Discover all test/rb/test_rb_*.rb files that have a matching .txt file.
static std::vector<LambdaTestInfo> discover_rb_tests() {
    std::vector<LambdaTestInfo> tests;
    const char* dir_path = "test/rb";

#ifdef _WIN32
    std::string search_path = std::string(dir_path) + "\\test_rb_*.rb";
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path.c_str(), &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            std::string filename = find_data.cFileName;
            if (filename.length() > 3 && filename.substr(filename.length() - 3) == ".rb") {
                std::string script_path = std::string(dir_path) + "/" + filename;
                std::string expected_path = script_path.substr(0, script_path.length() - 3) + ".txt";
                if (file_exists(expected_path)) {
                    LambdaTestInfo info;
                    info.script_path   = script_path;
                    info.expected_path = expected_path;
                    info.test_name     = get_test_name(script_path);
                    info.is_procedural = false;
                    if (!should_skip_rb_test(info.test_name))
                        tests.push_back(info);
                }
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
#else
    DIR* dir = opendir(dir_path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            // only test_rb_*.rb files
            if (filename.length() > 3 &&
                filename.substr(filename.length() - 3) == ".rb" &&
                filename.substr(0, 8) == "test_rb_") {
                std::string script_path = std::string(dir_path) + "/" + filename;
                std::string expected_path = script_path.substr(0, script_path.length() - 3) + ".txt";
                if (file_exists(expected_path)) {
                    LambdaTestInfo info;
                    info.script_path   = script_path;
                    info.expected_path = expected_path;
                    info.test_name     = get_test_name(script_path);
                    info.is_procedural = false;
                    if (!should_skip_rb_test(info.test_name))
                        tests.push_back(info);
                }
            }
        }
        closedir(dir);
    }
#endif

    std::sort(tests.begin(), tests.end(), [](const LambdaTestInfo& a, const LambdaTestInfo& b) {
        return a.test_name < b.test_name;
    });
    return tests;
}

// Global test list populated before main().
static std::vector<LambdaTestInfo> g_rb_tests;

//==============================================================================
// Per-script parallel runner
//
// Each script is run with:  ./lambda.exe rb --no-log <script>
// Results are stored in a shared map (writes are protected per-script by
// running one thread per script — no shared-state writes during execution).
//==============================================================================

struct RbTestResult {
    std::string output;   // stdout of the script
    int         status;   // exit code (0 = ok)
};

static std::unordered_map<std::string, RbTestResult> g_rb_results;
static std::mutex                                     g_rb_results_mutex;

// Run a single Ruby script and return its output.
static RbTestResult run_rb_script(const std::string& script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command),
             "lambda-jube.exe rb --no-log \"%s\" 2>&1", script_path.c_str());
#else
    snprintf(command, sizeof(command),
             "./lambda-jube.exe rb --no-log \"%s\" 2>&1", script_path.c_str());
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return {"", -1};
    }

    char buffer[4096];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    int raw_exit = pclose(pipe);
    int status   = WEXITSTATUS(raw_exit);

    return {output, status};
}

//==============================================================================
// Parameterized test class
//==============================================================================

class RbScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
public:
    static bool results_ready;

    static void SetUpTestSuite() {
        if (results_ready) return;

        // Run all scripts in parallel — one thread per script.
        std::vector<std::thread> threads;
        threads.reserve(g_rb_tests.size());

        for (const auto& info : g_rb_tests) {
            threads.emplace_back([script = info.script_path]() {
                RbTestResult result = run_rb_script(script);
                std::lock_guard<std::mutex> lock(g_rb_results_mutex);
                g_rb_results[script] = std::move(result);
            });
        }
        for (auto& t : threads) t.join();

        results_ready = true;
    }
};

bool RbScriptTest::results_ready = false;

TEST_P(RbScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();

    // Look up pre-computed result
    auto it = g_rb_results.find(info.script_path);
    ASSERT_NE(it, g_rb_results.end())
        << "No result for script: " << info.script_path;

    const RbTestResult& res = it->second;

    // Non-zero exit means the interpreter itself failed (not a test assertion)
    ASSERT_EQ(res.status, 0)
        << "lambda.exe rb exited with code " << res.status
        << " for: " << info.script_path
        << "\nOutput:\n" << res.output;

    // Strip NOTE/WARNING header lines emitted by debug builds
    std::string actual_str;
    {
        const std::string& raw = res.output;
        size_t pos = 0;
        while (pos < raw.size()) {
            size_t end_of_line = raw.find('\n', pos);
            if (end_of_line == std::string::npos) end_of_line = raw.size();
            std::string line = raw.substr(pos, end_of_line - pos);
            // Skip lines that contain [NOTE] or [WARNING] (debug build header)
            if (line.find("[NOTE]") == std::string::npos &&
                line.find("[WARNING]") == std::string::npos) {
                actual_str += line;
                if (end_of_line < raw.size()) actual_str += '\n';
            }
            pos = end_of_line + 1;
        }
    }

    // Trim trailing whitespace
    char* actual = strdup(actual_str.c_str());
    trim_trailing_whitespace(actual);

    // Read expected output
    char* expected = read_expected_output(info.expected_path.c_str());
    ASSERT_NE(expected, nullptr)
        << "Cannot read expected file: " << info.expected_path;

    ASSERT_STREQ(expected, actual)
        << "Output mismatch for: " << info.script_path
        << "  (expected " << strlen(expected) << " chars"
        << ", got " << strlen(actual) << " chars)";

    free(expected);
    free(actual);
}

// Test name generator: use the test_name field
static std::string RbTestNameGenerator(
    const ::testing::TestParamInfo<LambdaTestInfo>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    AutoDiscovered,
    RbScriptTest,
    ::testing::ValuesIn(g_rb_tests),
    RbTestNameGenerator
);

//==============================================================================
// Main
//==============================================================================

int main(int argc, char** argv) {
    // Discover Ruby tests before GTest initialises
    g_rb_tests = discover_rb_tests();

    printf("Discovered %zu Ruby transpiler tests:\n", g_rb_tests.size());
    for (const auto& t : g_rb_tests) {
        printf("  - %s\n", t.test_name.c_str());
    }
    printf("\n");

    ::testing::InitGoogleTest(&argc, argv);

    // Suppress Lambda log output unless running a specific filter
    std::string filter = GTEST_FLAG_GET(filter);
    if (filter == "*") {
#ifdef _WIN32
        _putenv_s("LAMBDA_NO_LOG", "1");
#else
        setenv("LAMBDA_NO_LOG", "1", 1);
#endif
    }

    return RUN_ALL_TESTS();
}
