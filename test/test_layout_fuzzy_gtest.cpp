/**
 * GTest runner for Layout Fuzzy Tests
 *
 * Auto-discovers HTML files under test/layout/data/fuzzy/ and runs each via
 * `./lambda.exe layout <html> --no-log -o /dev/null` to verify the layout
 * engine doesn't crash on fuzzer-found pathological inputs.
 *
 * Exit code convention:
 *   0   = layout completed successfully
 *   1   = graceful crash recovery (e.g. SIGBUS caught and handled)
 *   >1  = unrecoverable crash or signal (FAIL)
 *
 * Usage:
 *   ./test/test_layout_fuzzy_gtest.exe
 *   ./test/test_layout_fuzzy_gtest.exe --gtest_filter=FuzzyCrash.*table*
 *   ./test/test_layout_fuzzy_gtest.exe -j 4
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
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
    #define LAMBDA_EXE "lambda.exe"
    #define FUZZY_DIR "test\\layout\\data\\fuzzy"
    #define PATH_SEP "\\"
    #define NULL_DEV "NUL"
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define LAMBDA_EXE "./lambda.exe"
    #define FUZZY_DIR "test/layout/data/fuzzy"
    #define PATH_SEP "/"
    #define NULL_DEV "/dev/null"
#endif

// Timeout per file in seconds. Generous because a few cases legitimately do a
// lot of work: crash_extreme_dom_depth parses a ~10k-deep DOM (super-linear in
// depth) and takes ~5s solo in the ASan debug build, rising to ~14s under the
// heavy CPU contention of a full parallel `make test`. The timeout only guards
// against true hangs, so keep ample margin above that worst case.
#define FUZZY_TIMEOUT_SECONDS 60

// ============================================================================
// Test info
// ============================================================================

struct FuzzyTestInfo {
    std::string html_path;   // e.g. "test/layout/data/fuzzy/crash_table_abspos_in_grid.html"
    std::string test_name;   // e.g. "crash_table_abspos_in_grid"

    friend std::ostream& operator<<(std::ostream& os, const FuzzyTestInfo& info) {
        return os << info.test_name;
    }
};

// ============================================================================
// Helpers
// ============================================================================

static bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (f) { fclose(f); return true; }
    return false;
}

// ============================================================================
// Test discovery
// ============================================================================

static std::vector<FuzzyTestInfo> discover_fuzzy_tests() {
    std::vector<FuzzyTestInfo> tests;

#ifdef _WIN32
    std::string pattern = std::string(FUZZY_DIR) + "\\*.html";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return tests;
    do {
        std::string name = fd.cFileName;
        if (name.size() <= 5) continue;
        std::string base = name.substr(0, name.size() - 5); // strip .html
        FuzzyTestInfo info;
        info.html_path = std::string(FUZZY_DIR) + "\\" + name;
        info.test_name = base;
        tests.push_back(info);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(FUZZY_DIR);
    if (!dir) return tests;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() <= 5) continue;
        if (name.substr(name.size() - 5) != ".html") continue;

        std::string base = name.substr(0, name.size() - 5);
        FuzzyTestInfo info;
        info.html_path = std::string(FUZZY_DIR) + "/" + name;
        info.test_name = base;
        tests.push_back(info);
    }
    closedir(dir);

    std::sort(tests.begin(), tests.end(), [](const FuzzyTestInfo& a, const FuzzyTestInfo& b) {
        return a.test_name < b.test_name;
    });
#endif

    return tests;
}

// ============================================================================
// Global test list
// ============================================================================

static std::vector<FuzzyTestInfo> g_fuzzy_tests = discover_fuzzy_tests();

// ============================================================================
// Run a single fuzzy crash test
// ============================================================================

struct FuzzyTestResult {
    int exit_code;        // 0 = success, 1 = graceful recovery, >1 = crash
    bool timed_out;
    double elapsed_ms;
    std::string output;   // stderr/stdout from lambda.exe
};

static FuzzyTestResult run_fuzzy_test(const FuzzyTestInfo& info) {
    FuzzyTestResult result;
    result.exit_code = -1;
    result.timed_out = false;
    result.elapsed_ms = 0;

    auto t0 = std::chrono::steady_clock::now();
    const char* args[] = {
        LAMBDA_EXE, "layout", info.html_path.c_str(), "--no-log", "-o", NULL_DEV, NULL,
    };
    ShellOptions options = {0};
    options.merge_stderr = true;
    options.timeout_ms = FUZZY_TIMEOUT_SECONDS * 1000;
    // Centralized process-group cleanup prevents a timed-out layout child from leaking descendants.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (shell_result.stdout_buf) {
        size_t output_len = shell_result.stdout_len;
        if (output_len > 64 * 1024) output_len = 64 * 1024;
        result.output.assign(shell_result.stdout_buf, output_len);
    }
    result.timed_out = shell_result.timed_out;
    result.exit_code = result.timed_out ? -1 : shell_result.exit_code;
    shell_result_free(&shell_result);
    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

// ============================================================================
// Parallel execution
// ============================================================================

static std::vector<FuzzyTestResult> g_fuzzy_results;

static void run_fuzzy_tests_parallel(int jobs) {
    size_t n = g_fuzzy_tests.size();
    g_fuzzy_results.resize(n);
    if (n == 0) return;

    int num_threads = std::min(jobs, (int)n);
    std::atomic<size_t> next_idx{0};
    std::atomic<size_t> completed{0};
    std::mutex progress_mutex;

    auto worker = [&]() {
        while (true) {
            size_t idx = next_idx.fetch_add(1);
            if (idx >= n) break;
            g_fuzzy_results[idx] = run_fuzzy_test(g_fuzzy_tests[idx]);

            const FuzzyTestResult& result = g_fuzzy_results[idx];
            size_t done = completed.fetch_add(1) + 1;

            std::lock_guard<std::mutex> lock(progress_mutex);
            std::cout << "  [" << done << "/" << n << "] "
                      << g_fuzzy_tests[idx].test_name
                      << " finished in " << (int)result.elapsed_ms << "ms";
            if (result.timed_out) {
                std::cout << " (TIMEOUT)";
            } else if (result.exit_code == 1) {
                std::cout << " (graceful recovery)";
            } else if (result.exit_code != 0) {
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

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FuzzyCrashTest);

class FuzzyCrashTest : public ::testing::TestWithParam<size_t> {
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
        if (g_fuzzy_tests.empty()) {
            GTEST_SKIP() << "No fuzzy tests found in " FUZZY_DIR;
        }
    }
};

TEST_P(FuzzyCrashTest, NoCrash) {
    size_t idx = GetParam();
    if (idx >= g_fuzzy_tests.size()) {
        GTEST_SKIP() << "No test at index " << idx;
    }
    const FuzzyTestInfo& info = g_fuzzy_tests[idx];
    const FuzzyTestResult& result = g_fuzzy_results[idx];

    SCOPED_TRACE("Fuzzy: " + info.test_name);

    // Print timing
    std::cout << "  [" << info.test_name << "] "
              << (int)result.elapsed_ms << " ms"
              << (result.timed_out ? " (TIMEOUT)" : "")
              << (result.exit_code == 1 ? " (graceful recovery)" : "")
              << std::endl;

    // Print output on failure
    if (result.exit_code > 1 || result.timed_out) {
        std::cerr << "\n--- Output for " << info.test_name << " ---\n"
                  << result.output
                  << "--- End output ---\n";
    }

    EXPECT_FALSE(result.timed_out)
        << info.test_name << " timed out after " << FUZZY_TIMEOUT_SECONDS << "s";

    // Accept exit code 0 (success) or 1 (graceful crash recovery via _exit(1))
    EXPECT_LE(result.exit_code, 1)
        << info.test_name << " exited with code " << result.exit_code
        << (result.exit_code > 128 ? " (signal " + std::to_string(result.exit_code - 128) + ")" : "");
}

INSTANTIATE_TEST_SUITE_P(
    FuzzyCrash,
    FuzzyCrashTest,
    ::testing::Range(size_t(0), g_fuzzy_tests.size()),
    [](const ::testing::TestParamInfo<size_t>& info) {
        if (info.param < g_fuzzy_tests.size()) {
            std::string name = g_fuzzy_tests[info.param].test_name;
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
    std::cout << "║     Layout Fuzzy Test Suite                              ║\n";
    std::cout << "║                                                          ║\n";
    std::cout << "║  Runs HTML files from test/layout/data/fuzzy/ via:       ║\n";
    std::cout << "║    ./lambda.exe layout <html> --no-log -o /dev/null      ║\n";
    std::cout << "║  Verifies files don't crash (exit code <= 1).            ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    if (g_fuzzy_tests.empty()) {
        std::cerr << "WARNING: No HTML files found in " FUZZY_DIR "\n\n";
    } else {
        std::cout << "Found " << g_fuzzy_tests.size() << " fuzzy test(s), running with "
                  << jobs << " parallel job(s):\n";
        for (const auto& t : g_fuzzy_tests) {
            std::cout << "  • " << t.test_name << "\n";
        }
        std::cout << "\n";

        // Run all tests in parallel before GTest checks results
        auto t0 = std::chrono::steady_clock::now();
        run_fuzzy_tests_parallel(jobs);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "All " << g_fuzzy_tests.size() << " fuzzy files tested in "
                  << ms << " ms (" << jobs << " parallel jobs)\n\n";
    }

    return RUN_ALL_TESTS();
}
