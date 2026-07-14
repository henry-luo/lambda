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

extern "C" {
#include "../lib/shell.h"
}

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
    bool skip_headless;      // requires native GUI window (e.g. WKWebView tests)
    int estimated_wait_ms;   // explicit event waits, used only for launch scheduling
    int explicit_wait_count;
    int wait_before_assert_count;

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

static char* read_json_text(const std::string& json_path, size_t limit) {
    FILE* file = fopen(json_path.c_str(), "rb");
    if (!file) return nullptr;

    size_t capacity = limit;
    if (capacity == 0) {
        if (fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            return nullptr;
        }
        long file_size = ftell(file);
        if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
            fclose(file);
            return nullptr;
        }
        capacity = (size_t)file_size;
    }

    char* text = (char*)malloc(capacity + 1);
    if (!text) {
        fclose(file);
        return nullptr;
    }
    size_t length = fread(text, 1, capacity, file);
    fclose(file);
    text[length] = '\0';
    return text;
}

// Extract the top-level "html" field from a JSON event file.
// Reads only the first 2KB (the field must appear before the events array).
// Returns empty string if not found.
static std::string extract_html_from_json(const std::string& json_path) {
    char* json = read_json_text(json_path, 2048);
    std::string result;
    const char* key = json ? strstr(json, "\"html\"") : nullptr;
    if (key) {
        key += 6;
        while (*key == ' ' || *key == '\t' || *key == '\n' || *key == '\r') key++;
        if (*key == ':') key++;
        else key = nullptr;
    }
    if (key) {
        while (*key == ' ' || *key == '\t' || *key == '\n' || *key == '\r') key++;
        if (*key == '"') key++;
        else key = nullptr;
    }
    if (key) while (*key && *key != '"') result += *key++;
    free(json);
    return result;
}

// Check if a JSON event file has "skip_headless": true.
// These tests require a native GUI window (e.g. WKWebView) and cannot run headless.
static bool extract_skip_headless_from_json(const std::string& json_path) {
    char* json = read_json_text(json_path, 2048);
    const char* key = json ? strstr(json, "\"skip_headless\"") : nullptr;
    bool skip_headless = false;
    if (key) {
        key += 15;
        while (*key == ' ' || *key == '\t' || *key == '\n' || *key == '\r') key++;
        if (*key == ':') {
            key++;
            while (*key == ' ' || *key == '\t' || *key == '\n' || *key == '\r') key++;
            skip_headless = strncmp(key, "true", 4) == 0;
        }
    }
    free(json);
    return skip_headless;
}

typedef struct UiWaitStats {
    int total_ms;
    int count;
    int before_assert_count;
} UiWaitStats;

static UiWaitStats analyze_explicit_waits_from_json(const std::string& json_path) {
    UiWaitStats stats = {0};
    char* json = read_json_text(json_path, 0);
    if (!json) return stats;

    bool wait_pending = false;
    const char* cursor = json;
    while ((cursor = strstr(cursor, "\"type\"")) != nullptr) {
        const char* next_type = strstr(cursor + 6, "\"type\"");
        const char* colon = strchr(cursor + 6, ':');
        const char* value = colon ? strchr(colon + 1, '"') : nullptr;
        if (!value || (next_type && value >= next_type)) {
            cursor = next_type ? next_type : cursor + 6;
            continue;
        }

        const char* value_end = strchr(value + 1, '"');
        size_t type_length = value_end ? (size_t)(value_end - value - 1) : 0;
        bool is_wait = type_length == 4 && strncmp(value + 1, "wait", 4) == 0;
        bool is_log = type_length == 3 && strncmp(value + 1, "log", 3) == 0;
        bool is_assertion = type_length > 7 && strncmp(value + 1, "assert_", 7) == 0;

        if (is_wait) {
            stats.count++;
            const char* ms_key = strstr(value + 6, "\"ms\"");
            if (ms_key && (!next_type || ms_key < next_type)) {
                const char* ms_colon = strchr(ms_key + 4, ':');
                if (ms_colon && (!next_type || ms_colon < next_type)) {
                    long wait_ms = strtol(ms_colon + 1, nullptr, 10);
                    if (wait_ms > 0) stats.total_ms += (int)wait_ms;
                }
            }
            wait_pending = true;
        } else if (!is_log) {
            if (wait_pending && is_assertion) stats.before_assert_count++;
            wait_pending = false;
        }
        cursor = next_type ? next_type : cursor + 6;
    }
    free(json);
    return stats;
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
        info.skip_headless = extract_skip_headless_from_json(json_path);
        UiWaitStats wait_stats = analyze_explicit_waits_from_json(json_path);
        info.estimated_wait_ms = wait_stats.total_ms;
        info.explicit_wait_count = wait_stats.count;
        info.wait_before_assert_count = wait_stats.before_assert_count;
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
        info.skip_headless = extract_skip_headless_from_json(json_path);
        UiWaitStats wait_stats = analyze_explicit_waits_from_json(json_path);
        info.estimated_wait_ms = wait_stats.total_ms;
        info.explicit_wait_count = wait_stats.count;
        info.wait_before_assert_count = wait_stats.before_assert_count;
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

static std::string sanitize_gtest_param_name(const std::string& test_name) {
    std::string name = test_name;
    for (char& c : name) {
        if (c == '-' || c == '.' || c == ' ') c = '_';
    }
    return name;
}

static bool gtest_wildcard_match(const char* pattern, const char* text) {
    const char* star = nullptr;
    const char* retry_text = nullptr;
    while (*text) {
        if (*pattern == '?' || *pattern == *text) {
            pattern++;
            text++;
        } else if (*pattern == '*') {
            star = pattern++;
            retry_text = text;
        } else if (star) {
            pattern = star + 1;
            text = ++retry_text;
        } else {
            return false;
        }
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0';
}

static bool gtest_pattern_list_matches(const std::string& patterns, const std::string& name) {
    size_t start = 0;
    while (start <= patterns.size()) {
        size_t end = patterns.find(':', start);
        if (end == std::string::npos) end = patterns.size();
        if (end > start) {
            std::string pattern = patterns.substr(start, end - start);
            if (gtest_wildcard_match(pattern.c_str(), name.c_str())) return true;
        }
        if (end == patterns.size()) break;
        start = end + 1;
    }
    return false;
}

static bool gtest_filter_matches_ui_test(const UiTestInfo& info, const std::string& filter) {
    std::string positive = filter.empty() ? "*" : filter;
    std::string negative;
    size_t dash = positive.find('-');
    if (dash != std::string::npos) {
        negative = positive.substr(dash + 1);
        positive = positive.substr(0, dash);
        if (positive.empty()) positive = "*";
    }

    std::string full_name = "UIAutomation/UIAutomationTest.RunTest/" + sanitize_gtest_param_name(info.test_name);
    if (!gtest_pattern_list_matches(positive, full_name)) return false;
    if (!negative.empty() && gtest_pattern_list_matches(negative, full_name)) return false;
    return true;
}

// ============================================================================
// Run a single UI test via lambda.exe view
// ============================================================================

struct UiTestResult {
    bool executed = false;
    int exit_code = -1;
    int assertions_passed = 0;
    int assertions_failed = 0;
    long elapsed_ms = 0;
    std::string output;    // combined stdout + stderr
};

static UiTestResult run_ui_test(const UiTestInfo& info) {
    UiTestResult result;
    result.executed = true;
    auto t0 = std::chrono::steady_clock::now();

    // Build command: ./lambda.exe view <html> --event-file <json>
    // The window auto-closes when simulation completes (auto_close=true in EventSimContext).
    // Exit code: 0 = all assertions passed, 1 = one or more failed.
    const char* args[] = {
        LAMBDA_EXE, "view", info.html_path.c_str(),
        "--event-file", info.json_path.c_str(),
        "--headless", "--no-log", "--font-dir", "test/layout/data/font", NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    // Worker threads must launch argv directly; a shell adds process and quoting overhead.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (shell_result.stdout_buf) {
        result.output.assign(shell_result.stdout_buf, shell_result.stdout_len);
    }
    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    result.exit_code = shell_result.exit_code;
    shell_result_free(&shell_result);

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

static int compare_ui_indices_by_estimated_wait(const void* left, const void* right) {
    size_t left_index = *(const size_t*)left;
    size_t right_index = *(const size_t*)right;
    int left_wait = g_ui_tests[left_index].estimated_wait_ms;
    int right_wait = g_ui_tests[right_index].estimated_wait_ms;
    if (left_wait < right_wait) return 1;
    if (left_wait > right_wait) return -1;
    return strcmp(g_ui_tests[left_index].test_name.c_str(),
                  g_ui_tests[right_index].test_name.c_str());
}

static void run_ui_tests_parallel(const std::vector<size_t>& indices, int jobs) {
    size_t n = g_ui_tests.size();
    g_ui_results.resize(n);
    if (indices.empty()) return;

    int num_threads = std::min(jobs, (int)indices.size());
    std::atomic<size_t> next_idx{0};
    std::atomic<size_t> completed{0};
    std::mutex progress_mutex;

    auto worker = [&]() {
        while (true) {
            size_t idx = next_idx.fetch_add(1);
            if (idx >= indices.size()) break;
            size_t test_idx = indices[idx];
            g_ui_results[test_idx] = run_ui_test(g_ui_tests[test_idx]);

            const UiTestResult& result = g_ui_results[test_idx];
            int total_assertions = result.assertions_passed + result.assertions_failed;
            size_t done = completed.fetch_add(1) + 1;

            std::lock_guard<std::mutex> lock(progress_mutex);
            std::cout << "  [" << done << "/" << indices.size() << "] "
                      << g_ui_tests[test_idx].test_name << " finished in "
                      << result.elapsed_ms << "ms";
            if (total_assertions > 0) {
                std::cout << " (" << result.assertions_passed << "/" << total_assertions
                          << " assertions passed)";
            } else {
                std::cout << " (0 assertions)";
            }
            if (result.exit_code != 0) {
                std::cout << " (exit " << result.exit_code << ")";
            }
            std::cout << std::endl;
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

    if (info.skip_headless) {
        GTEST_SKIP() << info.test_name << " requires native GUI window (skip_headless=true)";
    }

    if (idx >= g_ui_results.size() || !g_ui_results[idx].executed) {
        if (g_ui_results.size() < g_ui_tests.size()) g_ui_results.resize(g_ui_tests.size());
        g_ui_results[idx] = run_ui_test(info);
    }
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
            return sanitize_gtest_param_name(g_ui_tests[info.param].test_name);
        }
        return std::string("test_") + std::to_string(info.param);
    }
);

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Child startup and event I/O leave CPU gaps, so 1.5x logical CPUs keeps
    // the worker pool busy without encoding a machine-specific job count.
    int cpu_count = (int)std::thread::hardware_concurrency();
    if (cpu_count <= 0) cpu_count = 1;
    int jobs = (cpu_count * 3) / 2;
    if (jobs <= 0) jobs = 1;
    const char* env_jobs = getenv("LAMBDA_UI_TEST_JOBS");
    if (env_jobs && *env_jobs) {
        jobs = atoi(env_jobs);
        if (jobs <= 0) jobs = 1;
    }
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
        std::string gtest_filter = ::testing::GTEST_FLAG(filter);
        std::vector<size_t> selected_indices;
        for (size_t idx = 0; idx < g_ui_tests.size(); idx++) {
            const UiTestInfo& info = g_ui_tests[idx];
            if (!info.skip_headless && gtest_filter_matches_ui_test(info, gtest_filter)) {
                selected_indices.push_back(idx);
            }
        }
        // Explicit-wait scenarios sorted late alphabetically were dominating the
        // worker tail; longest-estimated-first minimizes idle workers at shutdown.
        if (selected_indices.size() > 1) {
            qsort(selected_indices.data(), selected_indices.size(), sizeof(size_t),
                  compare_ui_indices_by_estimated_wait);
        }

        int selected_wait_count = 0;
        int selected_wait_ms = 0;
        int selected_wait_before_assert = 0;
        for (size_t idx : selected_indices) {
            selected_wait_count += g_ui_tests[idx].explicit_wait_count;
            selected_wait_ms += g_ui_tests[idx].estimated_wait_ms;
            selected_wait_before_assert += g_ui_tests[idx].wait_before_assert_count;
        }
        // A fixed sleep before an auto-waiting assertion is always redundant and
        // otherwise lets conservative delays silently accumulate in the baseline.
        if (selected_wait_before_assert > 0) {
            std::cerr << "ERROR: selected UI fixtures contain "
                      << selected_wait_before_assert
                      << " explicit wait(s) before assertions\n";
            return 2;
        }

        std::cout << "Found " << g_ui_tests.size() << " UI test(s), selected "
                  << selected_indices.size() << " for pre-run with "
                  << jobs << " parallel job(s)";
        if (gtest_filter != "*") std::cout << " using filter: " << gtest_filter;
        std::cout << ":\n";
        std::cout << "Runnable explicit JSON waits: " << selected_wait_count
                  << " events, " << selected_wait_ms << " ms aggregate\n";
        for (size_t idx : selected_indices) {
            std::cout << "  • " << g_ui_tests[idx].test_name << "\n";
        }
        std::cout << "\n";

        // Run selected tests in parallel before GTest checks results.
        auto t0 = std::chrono::steady_clock::now();
        run_ui_tests_parallel(selected_indices, jobs);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << selected_indices.size() << " selected test(s) executed in "
                  << ms << " ms (" << jobs << " parallel jobs)\n\n";
    }

    return RUN_ALL_TESTS();
}
