/**
 * GTest runner for UI Automation Tests
 *
 * Auto-discovers HTML/JSON test pairs under test/ui/ and runs each as a
 * separate GTest case via `./lambda.exe view <html> --event-file <json>`.
 *
 * Exit code convention (enforced by radiant/window.cpp):
 *   0 = all assertions passed
 *   1 = one or more assertions failed
 *
 * The event sim prints a summary to stderr:
 *   ========================================
 *    Assertions: N passed, M failed
 *    Result: PASS / FAIL
 *   ========================================
 *
 * Usage:
 *   ./test/test_ui_automation_gtest.exe
 *   ./test/test_ui_automation_gtest.exe --gtest_filter=UIAutomation.*click*
 *
 * Note: Tests require a graphical display. On headless CI, set the
 *       environment variable DISPLAY or use xvfb-run.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #include <windows.h>
    #include <io.h>
    #define popen  _popen
    #define pclose _pclose
    #define WEXITSTATUS(s) (s)
    #define LAMBDA_EXE "lambda.exe"
    #define UI_TESTS_DIR "test\\ui"
    #define PATH_SEP "\\"
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #define LAMBDA_EXE "./lambda.exe"
    #define UI_TESTS_DIR "test/ui"
    #define PATH_SEP "/"
#endif

// ============================================================================
// Test info struct
// ============================================================================

struct UiTestInfo {
    std::string html_path;   // e.g. "test/ui/test_click_text.html"
    std::string json_path;   // e.g. "test/ui/test_click_text.json"
    std::string test_name;   // e.g. "test_click_text"

    friend std::ostream& operator<<(std::ostream& os, const UiTestInfo& info) {
        return os << info.test_name;
    }
};

// ============================================================================
// Test discovery
// ============================================================================

static bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (f) { fclose(f); return true; }
    return false;
}

// Extract the top-level "html" field from a JSON event file.
// Reads only the first 2KB (the field must appear before the events array).
// Returns empty string if not found.
static std::string extract_html_from_json(const std::string& json_path) {
    FILE* f = fopen(json_path.c_str(), "r");
    if (!f) return "";
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    const char* key = strstr(buf, "\"html\"");
    if (!key) return "";
    key += 6;
    while (*key == ' ' || *key == '\t' || *key == '\n' || *key == '\r') key++;
    if (*key != ':') return "";
    key++;
    while (*key == ' ' || *key == '\t' || *key == '\n' || *key == '\r') key++;
    if (*key != '"') return "";
    key++;
    std::string result;
    while (*key && *key != '"') result += *key++;
    return result;
}

// Discover all *.json files in test/ui/.
// For each JSON the HTML target is resolved in order:
//   1. "html" field inside the JSON (can reference any path, e.g. a baseline file)
//   2. Sibling .html file with the same base name
// JSON files with neither a valid "html" field nor a sibling HTML are skipped.
static std::vector<UiTestInfo> discover_ui_tests() {
    std::vector<UiTestInfo> tests;

#ifdef _WIN32
    std::string pattern = std::string(UI_TESTS_DIR) + "\\*.json";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return tests;
    do {
        std::string json_name = fd.cFileName;
        if (json_name.size() <= 5) continue;
        std::string base = json_name.substr(0, json_name.size() - 5);
        std::string json_path = std::string(UI_TESTS_DIR) + "\\" + json_name;

        std::string html_path = extract_html_from_json(json_path);
        if (html_path.empty()) {
            std::string sibling = std::string(UI_TESTS_DIR) + "\\" + base + ".html";
            if (file_exists(sibling)) html_path = sibling;
        }
        if (html_path.empty() || !file_exists(html_path)) continue;

        UiTestInfo info;
        info.html_path = html_path;
        info.json_path = json_path;
        info.test_name = base;
        tests.push_back(info);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(UI_TESTS_DIR);
    if (!dir) return tests;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() <= 5) continue;
        if (name.substr(name.size() - 5) != ".json") continue;

        std::string base = name.substr(0, name.size() - 5);
        std::string json_path = std::string(UI_TESTS_DIR) + "/" + name;

        // Resolve HTML: from "html" field in JSON, or sibling .html
        std::string html_path = extract_html_from_json(json_path);
        if (html_path.empty()) {
            std::string sibling = std::string(UI_TESTS_DIR) + "/" + base + ".html";
            if (file_exists(sibling)) html_path = sibling;
        }
        if (html_path.empty() || !file_exists(html_path)) continue;

        UiTestInfo info;
        info.html_path = html_path;
        info.json_path = json_path;
        info.test_name = base;
        tests.push_back(info);
    }
    closedir(dir);

    std::sort(tests.begin(), tests.end(), [](const UiTestInfo& a, const UiTestInfo& b) {
        return a.test_name < b.test_name;
    });
#endif

    return tests;
}

// ============================================================================
// Global test list (populated once in main)
// ============================================================================

static std::vector<UiTestInfo> g_ui_tests = discover_ui_tests();

// ============================================================================
// Run a single UI test via lambda.exe view
// ============================================================================

struct UiTestResult {
    int exit_code;
    int assertions_passed;
    int assertions_failed;
    std::string output;    // combined stdout + stderr
};

static UiTestResult run_ui_test(const UiTestInfo& info) {
    UiTestResult result;
    result.exit_code = -1;
    result.assertions_passed = 0;
    result.assertions_failed = 0;

    // Build command: ./lambda.exe view <html> --event-file <json>
    // The window auto-closes when simulation completes (auto_close=true in EventSimContext).
    // Exit code: 0 = all assertions passed, 1 = one or more failed.
    std::string cmd = std::string(LAMBDA_EXE)
        + " view " + info.html_path
        + " --event-file " + info.json_path
        + " --headless"
        + " --no-log"
        + " --font-dir test/layout/data/font"
        + " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.output = "Failed to popen: " + cmd;
        return result;
    }

    char buf[512];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        result.output += buf;
    }

    int status = pclose(pipe);
    result.exit_code = WEXITSTATUS(status);

    // Parse assertion counts from the event_sim output
    // Pattern: " Assertions: N passed, M failed"
    const char* assert_line = strstr(result.output.c_str(), "Assertions:");
    if (assert_line) {
        int p = 0, f = 0;
        if (sscanf(assert_line, "Assertions: %d passed, %d failed", &p, &f) == 2) {
            result.assertions_passed = p;
            result.assertions_failed = f;
        }
    }

    return result;
}

// ============================================================================
// Parallel execution: run all lambda.exe processes upfront
// ============================================================================

static std::vector<UiTestResult> g_ui_results;

static void run_ui_tests_parallel(int jobs) {
    size_t n = g_ui_tests.size();
    g_ui_results.resize(n);
    if (n == 0) return;

    int num_threads = std::min(jobs, (int)n);
    std::atomic<size_t> next_idx{0};

    auto worker = [&]() {
        while (true) {
            size_t idx = next_idx.fetch_add(1);
            if (idx >= n) break;
            g_ui_results[idx] = run_ui_test(g_ui_tests[idx]);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }
}

// ============================================================================
// Parameterized test fixture
// ============================================================================

class UIAutomationTest : public ::testing::TestWithParam<size_t> {
protected:
    void SetUp() override {
        if (!file_exists(LAMBDA_EXE)) {
            GTEST_SKIP() << "lambda.exe not found - run 'make build' first";
        }
#ifndef _WIN32
        if (access(LAMBDA_EXE, X_OK) != 0) {
            GTEST_SKIP() << "lambda.exe is not executable";
        }
#endif
        if (g_ui_tests.empty()) {
            GTEST_SKIP() << "No UI tests found in " UI_TESTS_DIR;
        }
    }
};

TEST_P(UIAutomationTest, RunTest) {
    size_t idx = GetParam();
    if (idx >= g_ui_tests.size()) {
        GTEST_SKIP() << "No test at index " << idx;
    }
    const UiTestInfo& info = g_ui_tests[idx];

    SCOPED_TRACE("UI test: " + info.test_name);

    const UiTestResult& result = g_ui_results[idx];

    // Always print assertion summary so we can verify tests actually assert
    int total_assertions = result.assertions_passed + result.assertions_failed;
    if (total_assertions > 0) {
        std::cout << "  [" << info.test_name << "] "
                  << result.assertions_passed << "/" << total_assertions
                  << " assertions passed" << std::endl;
    } else {
        std::cout << "  [" << info.test_name << "] WARNING: 0 assertions"
                  << std::endl;
    }

    // Print full output on failure for easier debugging
    if (result.exit_code != 0 || result.assertions_failed > 0) {
        std::cerr << "\n--- Output for " << info.test_name << " ---\n"
                  << result.output
                  << "--- End output ---\n";
    }

    EXPECT_EQ(result.exit_code, 0)
        << info.test_name << " exited with code " << result.exit_code;

    EXPECT_GT(total_assertions, 0)
        << info.test_name << ": test has no assertions - add assert_* events to the JSON";

    if (total_assertions > 0) {
        EXPECT_EQ(result.assertions_failed, 0)
            << info.test_name << ": "
            << result.assertions_failed << " assertion(s) failed, "
            << result.assertions_passed << " passed";
    }
}

// Instantiation: one test case per discovered HTML/JSON pair
INSTANTIATE_TEST_SUITE_P(
    UIAutomation,
    UIAutomationTest,
    ::testing::Range(size_t(0), g_ui_tests.size()),
    [](const ::testing::TestParamInfo<size_t>& info) {
        if (info.param < g_ui_tests.size()) {
            std::string name = g_ui_tests[info.param].test_name;
            // Replace characters invalid in GTest param names
            for (char& c : name) {
                if (c == '-' || c == '.' || c == ' ') c = '_';
            }
            return name;
        }
        return std::string("test_") + std::to_string(info.param);
    }
);

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Parse -j N for parallelism (default: hardware concurrency)
    int jobs = (int)std::thread::hardware_concurrency();
    if (jobs <= 0) jobs = 4;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--jobs") == 0) && i + 1 < argc) {
            jobs = atoi(argv[++i]);
            if (jobs <= 0) jobs = 1;
        }
    }

    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     UI Automation Test Suite                             ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  Runs HTML/JSON test pairs from test/ui/ via:             ║\n";
    std::cout << "║    ./lambda.exe view <html> --event-file <json>           ║\n";
    std::cout << "║  Window auto-closes when simulation completes.            ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  Requirements:                                            ║\n";
    std::cout << "║  • lambda.exe built (run 'make build')                    ║\n";
    std::cout << "║  • test/ui/test_*.html + test_*.json pairs                ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    if (g_ui_tests.empty()) {
        std::cerr << "WARNING: No UI test pairs found in " UI_TESTS_DIR "\n";
        std::cerr << "         Create test_*.html + test_*.json pairs to add tests.\n\n";
    } else {
        std::cout << "Found " << g_ui_tests.size() << " UI test(s), running with "
                  << jobs << " parallel job(s):\n";
        for (const auto& t : g_ui_tests) {
            std::cout << "  • " << t.test_name << "\n";
        }
        std::cout << "\n";

        // Run all tests in parallel before GTest checks results
        auto t0 = std::chrono::steady_clock::now();
        run_ui_tests_parallel(jobs);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "All " << g_ui_tests.size() << " tests executed in "
                  << ms << " ms (" << jobs << " parallel jobs)\n\n";
    }

    return RUN_ALL_TESTS();
}
