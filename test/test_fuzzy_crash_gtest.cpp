/**
 * GTest runner for Fuzzy Crash Tests
 *
 * Auto-discovers HTML files under test/layout/data/fuzzy/ and runs each via
 * `./lambda.exe layout <html> --no-log -o /dev/null` to verify they don't crash.
 *
 * Exit code convention:
 *   0   = layout completed successfully
 *   1   = graceful crash recovery (e.g. SIGBUS caught and handled)
 *   >1  = unrecoverable crash or signal (FAIL)
 *
 * Usage:
 *   ./test/test_fuzzy_crash_gtest.exe
 *   ./test/test_fuzzy_crash_gtest.exe --gtest_filter=FuzzyCrash.*table*
 *   ./test/test_fuzzy_crash_gtest.exe -j 4
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

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #include <windows.h>
    #include <io.h>
    #define WEXITSTATUS(s) (s)
    #define LAMBDA_EXE "lambda.exe"
    #define FUZZY_DIR "test\\layout\\data\\fuzzy"
    #define PATH_SEP "\\"
    #define NULL_DEV "NUL"
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <signal.h>
    #define LAMBDA_EXE "./lambda.exe"
    #define FUZZY_DIR "test/layout/data/fuzzy"
    #define PATH_SEP "/"
    #define NULL_DEV "/dev/null"
#endif

// Timeout per file in seconds
#define FUZZY_TIMEOUT_SECONDS 15

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

#ifdef _WIN32

static FuzzyTestResult run_fuzzy_test(const FuzzyTestInfo& info) {
    FuzzyTestResult result;
    result.exit_code = -1;
    result.timed_out = false;
    result.elapsed_ms = 0;

    std::string cmd = std::string(LAMBDA_EXE) + " layout " + info.html_path + " --no-log -o " NULL_DEV " 2>&1";

    auto t0 = std::chrono::steady_clock::now();
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        result.output = "Failed to popen: " + cmd;
        return result;
    }

    char buf[512];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        result.output += buf;
    }

    int status = _pclose(pipe);
    result.exit_code = status;
    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

#else

static FuzzyTestResult run_fuzzy_test(const FuzzyTestInfo& info) {
    FuzzyTestResult result;
    result.exit_code = -1;
    result.timed_out = false;
    result.elapsed_ms = 0;

    auto t0 = std::chrono::steady_clock::now();

    // Use fork/exec with timeout for crash safety
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result.output = "Failed to create pipe";
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.output = "Failed to fork";
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        // Create new process group so we can kill the whole group on timeout
        setpgid(0, 0);
        execl(LAMBDA_EXE, LAMBDA_EXE, "layout", info.html_path.c_str(),
              "--no-log", "-o", NULL_DEV, (char*)NULL);
        _exit(127); // exec failed
    }

    // Parent process
    close(pipefd[1]);

    // Read output with timeout
    char buf[512];
    fd_set fds;
    struct timeval tv;
    bool child_done = false;

    while (!child_done) {
        FD_ZERO(&fds);
        FD_SET(pipefd[0], &fds);
        tv.tv_sec = FUZZY_TIMEOUT_SECONDS;
        tv.tv_usec = 0;

        int sel = select(pipefd[0] + 1, &fds, NULL, NULL, &tv);
        if (sel > 0) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                if (result.output.size() < 64 * 1024) { // cap at 64KB
                    result.output += buf;
                }
            } else {
                child_done = true; // EOF
            }
        } else {
            // Timeout or error
            result.timed_out = true;
            kill(-pid, SIGKILL); // kill process group
            child_done = true;
        }
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (result.timed_out) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
    }

    return result;
}

#endif

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

    auto worker = [&]() {
        while (true) {
            size_t idx = next_idx.fetch_add(1);
            if (idx >= n) break;
            g_fuzzy_results[idx] = run_fuzzy_test(g_fuzzy_tests[idx]);
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
    std::cout << "║     Fuzzy Crash Test Suite                               ║\n";
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
