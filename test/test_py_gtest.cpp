//==============================================================================
// Python Transpiler Tests - Auto-Discovery Based
//
// Auto-discovers test/py/test_py_*.py files that have a matching .txt
// expected-output file and runs them in parallel via `./lambda.exe py`.
//
// Each script is spawned in its own thread (SetUpTestSuite), results are
// cached in a map, and TEST_P compares actual vs expected output.
//==============================================================================

#include "test_lambda_helpers.hpp"

#include <atomic>
#include <mutex>

//==============================================================================
// Python test discovery
//==============================================================================

//==============================================================================
// Skip list — tests that are known broken / not yet passing
//==============================================================================

static const char* PY_SKIP_TESTS[] = {
    // test_py_import: imports test/py/utils.py (filesystem-based import);
    // crashes with Bus error — Phase E (full package system) not yet implemented.
    "py_test_py_import",
};
static const size_t NUM_PY_SKIP_TESTS = sizeof(PY_SKIP_TESTS) / sizeof(PY_SKIP_TESTS[0]);

static bool should_skip_py_test(const std::string& test_name) {
    for (size_t i = 0; i < NUM_PY_SKIP_TESTS; i++) {
        if (test_name == PY_SKIP_TESTS[i]) return true;
    }
    return false;
}

// Discover all test/py/test_py_*.py files that have a matching .txt file.
static std::vector<LambdaTestInfo> discover_py_tests() {
    std::vector<LambdaTestInfo> tests;
    const char* dir_path = "test/py";

#ifdef _WIN32
    std::string search_path = std::string(dir_path) + "\\test_py_*.py";
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path.c_str(), &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            std::string filename = find_data.cFileName;
            if (filename.length() > 3 && filename.substr(filename.length() - 3) == ".py") {
                std::string script_path = std::string(dir_path) + "/" + filename;
                std::string expected_path = script_path.substr(0, script_path.length() - 3) + ".txt";
                if (file_exists(expected_path)) {
                    LambdaTestInfo info;
                    info.script_path   = script_path;
                    info.expected_path = expected_path;
                    info.test_name     = get_test_name(script_path);
                    info.is_procedural = false;
                    if (!should_skip_py_test(info.test_name))
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
            // only test_py_*.py files
            if (filename.length() > 3 &&
                filename.substr(filename.length() - 3) == ".py" &&
                filename.substr(0, 8) == "test_py_") {
                std::string script_path = std::string(dir_path) + "/" + filename;
                std::string expected_path = script_path.substr(0, script_path.length() - 3) + ".txt";
                if (file_exists(expected_path)) {
                    LambdaTestInfo info;
                    info.script_path   = script_path;
                    info.expected_path = expected_path;
                    info.test_name     = get_test_name(script_path);
                    info.is_procedural = false;
                    if (!should_skip_py_test(info.test_name))
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
static std::vector<LambdaTestInfo> g_py_tests;

//==============================================================================
// Per-script parallel runner
//
// Each script is run with:  ./lambda.exe py --no-log <script>
// Results are stored in a shared map (writes are protected per-script by
// running one thread per script — no shared-state writes during execution).
//==============================================================================

struct PyTestResult {
    std::string output;   // stdout of the script
    int         status;   // exit code (0 = ok)
};

static std::unordered_map<std::string, PyTestResult> g_py_results;
static std::mutex                                     g_py_results_mutex;

// Run a single Python script and return its output.
static PyTestResult run_py_script(const std::string& script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command),
             "lambda-jube.exe py --no-log \"%s\" 2>&1", script_path.c_str());
#else
    snprintf(command, sizeof(command),
             "./lambda-jube.exe py --no-log \"%s\" 2>&1", script_path.c_str());
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

class PyScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
public:
    static bool results_ready;

    static void SetUpTestSuite() {
        if (results_ready) return;

        // Run all scripts in parallel — one thread per script.
        // Python scripts are short-lived (< 1 s each), so this is safe.
        std::vector<std::thread> threads;
        threads.reserve(g_py_tests.size());

        for (const auto& info : g_py_tests) {
            threads.emplace_back([script = info.script_path]() {
                PyTestResult result = run_py_script(script);
                std::lock_guard<std::mutex> lock(g_py_results_mutex);
                g_py_results[script] = std::move(result);
            });
        }
        for (auto& t : threads) t.join();

        results_ready = true;
    }
};

bool PyScriptTest::results_ready = false;

TEST_P(PyScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();

    // Look up pre-computed result
    auto it = g_py_results.find(info.script_path);
    ASSERT_NE(it, g_py_results.end())
        << "No result for script: " << info.script_path;

    const PyTestResult& res = it->second;

    // Non-zero exit means the interpreter itself failed (not a test assertion)
    ASSERT_EQ(res.status, 0)
        << "lambda.exe py exited with code " << res.status
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
static std::string PyTestNameGenerator(
    const ::testing::TestParamInfo<LambdaTestInfo>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    AutoDiscovered,
    PyScriptTest,
    ::testing::ValuesIn(g_py_tests),
    PyTestNameGenerator
);

//==============================================================================
// Main
//==============================================================================

int main(int argc, char** argv) {
    // Discover Python tests before GTest initialises
    g_py_tests = discover_py_tests();

    printf("Discovered %zu Python transpiler tests:\n", g_py_tests.size());
    for (const auto& t : g_py_tests) {
        printf("  - %s\n", t.test_name.c_str());
    }
    printf("\n");

    ::testing::InitGoogleTest(&argc, argv);

    // Suppress Lambda log output unless running a specific filter
    std::string filter = ::testing::GTEST_FLAG(filter);
    if (filter == "*") {
#ifdef _WIN32
        _putenv_s("LAMBDA_NO_LOG", "1");
#else
        setenv("LAMBDA_NO_LOG", "1", 1);
#endif
    }

    return RUN_ALL_TESTS();
}
