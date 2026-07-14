/**
 * GTest runner for Page Load (Headless) Tests
 *
 * Auto-discovers HTML files under test/layout/data/page/ and
 * test/layout/data/markdown/ and runs each via
 * `./lambda.exe view <html> --headless --no-log` to verify they:
 *   1. Load without crashing (exit code 0)
 *   2. Complete layout within 4s and render within 2s
 *   3. Keep page-induced memory growth within budget
 *
 * Timing is parsed from [LAYOUT_PROF] and [RENDER_PROF] stderr output.
 *
 * Usage:
 *   ./test/test_page_load_gtest.exe
 *   ./test/test_page_load_gtest.exe --gtest_filter=PageLoad.*cern*
 *   ./test/test_page_load_gtest.exe -j 4
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
    #define PAGE_DIR "test\\layout\\data\\page"
    #define MARKDOWN_DIR "test\\layout\\data\\markdown"
    #define PATH_SEP "\\"
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #include <sys/resource.h>
    #include <sys/time.h>
    #include <signal.h>
    #define LAMBDA_EXE "./lambda.exe"
    #define PAGE_DIR "test/layout/data/page"
    #define MARKDOWN_DIR "test/layout/data/markdown"
    #define PATH_SEP "/"
#endif

// Timeout per page in seconds
#define PAGE_TIMEOUT_SECONDS 15

// ============================================================================
// Test info
// ============================================================================

struct PageTestInfo {
    std::string html_path;   // e.g. "test/layout/data/page/cern.html"
    std::string test_name;   // e.g. "cern"

    friend std::ostream& operator<<(std::ostream& os, const PageTestInfo& info) {
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

static void discover_from_dir(const char* dir_path, std::vector<PageTestInfo>& tests) {
#ifdef _WIN32
    std::string pattern = std::string(dir_path) + "\\*.html";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string name = fd.cFileName;
        if (name.size() <= 5) continue;
        std::string base = name.substr(0, name.size() - 5); // strip .html
        PageTestInfo info;
        info.html_path = std::string(dir_path) + "\\" + name;
        info.test_name = base;
        tests.push_back(info);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(dir_path);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() <= 5) continue;
        if (name.substr(name.size() - 5) != ".html") continue;

        std::string base = name.substr(0, name.size() - 5);
        PageTestInfo info;
        info.html_path = std::string(dir_path) + "/" + name;
        info.test_name = base;
        tests.push_back(info);
    }
    closedir(dir);
#endif
}

static std::vector<PageTestInfo> discover_page_tests() {
    std::vector<PageTestInfo> tests;
    discover_from_dir(PAGE_DIR, tests);
    discover_from_dir(MARKDOWN_DIR, tests);

    std::sort(tests.begin(), tests.end(), [](const PageTestInfo& a, const PageTestInfo& b) {
        return a.test_name < b.test_name;
    });

    return tests;
}

// ============================================================================
// Global test list
// ============================================================================

static std::vector<PageTestInfo> g_page_tests = discover_page_tests();

// ============================================================================
// Run a single page load test
// ============================================================================

// Layout/render time limits (seconds)
#define MAX_LAYOUT_SECONDS 4.0
#define MAX_RENDER_SECONDS 2.0

// Peak RSS limit per page (bytes). A single lambda.exe child rendering one
// HTML/MD page should comfortably fit in this budget; exceeding it usually
// signals a memory leak or pathological growth in font/css/layout pipelines.
// macOS-only enforcement: getrusage(RUSAGE_CHILDREN) reports peak RSS in bytes
// on Darwin and KB on Linux — we normalise via wait4 + a per-OS multiplier.
#define MAX_PEAK_RSS_BYTES (160ULL * 1024 * 1024)  // 160 MB hard cap (RSS, includes shared OS pages)
#define MAX_PEAK_FOOTPRINT_BYTES (180ULL * 1024 * 1024)  // fallback absolute cap when baseline telemetry is unavailable
#define MAX_PAGE_FOOTPRINT_DELTA_BYTES (100ULL * 1024 * 1024)  // page-induced private memory growth cap
#define MAX_PAGE_MEMTRACK_LIVE_BYTES (16ULL * 1024 * 1024)  // Lambda-owned live allocation cap after cleanup

#ifdef __APPLE__
    #define RUSAGE_MAXRSS_TO_BYTES(x) ((uint64_t)(x))
#else
    #define RUSAGE_MAXRSS_TO_BYTES(x) ((uint64_t)(x) * 1024ULL)
#endif

struct PageTestResult {
    int exit_code;        // 0 = success, non-zero = failure
    bool timed_out;
    double elapsed_ms;
    double layout_ms;     // from [LAYOUT_PROF], -1 if not found
    double render_ms;     // from [RENDER_PROF], -1 if not found
    uint64_t peak_rss_bytes;  // child's peak resident set size (0 if unknown)
    uint64_t baseline_footprint_bytes;  // child's initial phys_footprint (macOS only, 0 if unknown)
    uint64_t current_footprint_bytes;  // child's retained phys_footprint after cleanup (macOS only, 0 if unknown)
    uint64_t peak_footprint_bytes;  // child's peak phys_footprint including transient spikes (macOS only, 0 if unknown)
    bool memtrack_live_seen;  // true when Lambda-owned live allocation telemetry was emitted
    uint64_t memtrack_live_bytes;  // Lambda-owned tracked bytes still live at shutdown
    uint64_t memtrack_live_count;  // Lambda-owned tracked allocation count still live at shutdown
    std::string output;   // stderr/stdout from lambda.exe
};

// Parse a profiling line like "[LAYOUT_PROF] layout_html_root: 123.4ms"
// or "[RENDER_PROF] render_block_view: 456.7ms ..." and return the ms value.
// Returns -1 if the tag is not found.
static double parse_prof_ms(const std::string& output, const char* tag) {
    size_t pos = output.find(tag);
    if (pos == std::string::npos) return -1;
    // Find the first number after the tag
    pos += strlen(tag);
    while (pos < output.size() && (output[pos] < '0' || output[pos] > '9')) pos++;
    if (pos >= output.size()) return -1;
    return atof(output.c_str() + pos);
}

// Parse "[PEAK_FOOTPRINT] <bytes>\n" emitted by the child on macOS exit.
// Returns 0 if the tag is not found.
static uint64_t parse_peak_footprint(const std::string& output) {
    const char* tag = "[PEAK_FOOTPRINT]";
    size_t pos = output.find(tag);
    if (pos == std::string::npos) return 0;
    pos += strlen(tag);
    while (pos < output.size() && (output[pos] < '0' || output[pos] > '9')) pos++;
    if (pos >= output.size()) return 0;
    return strtoull(output.c_str() + pos, nullptr, 10);
}

static uint64_t parse_tagged_uint64(const std::string& output, const char* tag, const char* key) {
    size_t pos = output.find(tag);
    if (pos == std::string::npos) return 0;
    pos = output.find(key, pos);
    if (pos == std::string::npos) return 0;
    pos += strlen(key);
    while (pos < output.size() && (output[pos] < '0' || output[pos] > '9')) pos++;
    if (pos >= output.size()) return 0;
    return strtoull(output.c_str() + pos, nullptr, 10);
}

static uint64_t parse_memtrack_live_bytes(const std::string& output) {
    return parse_tagged_uint64(output, "[MEMTRACK_LIVE]", "bytes=");
}

static uint64_t parse_memtrack_live_count(const std::string& output) {
    return parse_tagged_uint64(output, "[MEMTRACK_LIVE]", "count=");
}

static bool parse_memtrack_live_seen(const std::string& output) {
    return output.find("[MEMTRACK_LIVE]") != std::string::npos;
}

// Parse a specific VIEW_MEM_STAGES footprint. This is used for retained page
// footprint so temporary compile/render spikes do not masquerade as leaks.
static uint64_t parse_memstage_footprint(const std::string& output, const char* stage) {
    size_t pos = output.find(stage);
    if (pos == std::string::npos) return 0;
    const char* key = "footprint=";
    pos = output.find(key, pos);
    if (pos == std::string::npos) return 0;
    pos += strlen(key);
    while (pos < output.size() && (output[pos] < '0' || output[pos] > '9')) pos++;
    if (pos >= output.size()) return 0;
    return strtoull(output.c_str() + pos, nullptr, 10) * 1024ULL * 1024ULL;
}

// Parse the initial macOS footprint from VIEW_MEM_STAGES output. The absolute
// lambda.exe baseline includes fixed runtime/JIT/framework allocation, so page
// load tests enforce page-induced growth when this telemetry is available.
static uint64_t parse_before_load_footprint(const std::string& output) {
    return parse_memstage_footprint(output, "[MEMSTAGE] before-load");
}

static uint64_t parse_after_cleanup_footprint(const std::string& output) {
    uint64_t after_mempool = parse_memstage_footprint(output, "[MEMSTAGE] after-mempool-cleanup");
    if (after_mempool > 0) return after_mempool;
    uint64_t after_cleanup = parse_memstage_footprint(output, "[MEMSTAGE] after-cleanup");
    if (after_cleanup > 0) return after_cleanup;
    return parse_memstage_footprint(output, "[MEMSTAGE] after-render");
}

#ifdef _WIN32

static PageTestResult run_page_test(const PageTestInfo& info) {
    PageTestResult result;
    result.exit_code = -1;
    result.timed_out = false;
    result.elapsed_ms = 0;
    result.layout_ms = -1;
    result.render_ms = -1;
    result.peak_rss_bytes = 0;
    result.baseline_footprint_bytes = 0;
    result.current_footprint_bytes = 0;
    result.peak_footprint_bytes = 0;
    result.memtrack_live_seen = false;
    result.memtrack_live_bytes = 0;
    result.memtrack_live_count = 0;

    auto t0 = std::chrono::steady_clock::now();
    const ShellEnvEntry env[] = {
        {"VIEW_MEM_STAGES", "1"},
        {NULL, NULL},
    };
    const char* args[] = {
        LAMBDA_EXE, "view", info.html_path.c_str(), "--headless", "--no-log", NULL,
    };
    ShellOptions options = {0};
    options.env = env;
    options.merge_stderr = true;
    options.timeout_ms = PAGE_TIMEOUT_SECONDS * 1000;
    // Per-child telemetry avoids mutating the parent environment while workers launch in parallel.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (shell_result.stdout_buf) {
        result.output.assign(shell_result.stdout_buf, shell_result.stdout_len);
    }
    result.timed_out = shell_result.timed_out;
    result.exit_code = result.timed_out ? -1 : shell_result.exit_code;
    shell_result_free(&shell_result);
    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.layout_ms = parse_prof_ms(result.output, "[LAYOUT_PROF]");
    result.render_ms = parse_prof_ms(result.output, "[RENDER_PROF]");
    result.baseline_footprint_bytes = parse_before_load_footprint(result.output);
    result.current_footprint_bytes = parse_after_cleanup_footprint(result.output);
    result.peak_footprint_bytes = parse_peak_footprint(result.output);
    result.memtrack_live_seen = parse_memtrack_live_seen(result.output);
    result.memtrack_live_bytes = parse_memtrack_live_bytes(result.output);
    result.memtrack_live_count = parse_memtrack_live_count(result.output);
    return result;
}

#else

static PageTestResult run_page_test(const PageTestInfo& info) {
    PageTestResult result;
    result.exit_code = -1;
    result.timed_out = false;
    result.elapsed_ms = 0;
    result.layout_ms = -1;
    result.render_ms = -1;
    result.peak_rss_bytes = 0;
    result.baseline_footprint_bytes = 0;
    result.current_footprint_bytes = 0;
    result.peak_footprint_bytes = 0;
    result.memtrack_live_seen = false;
    result.memtrack_live_bytes = 0;
    result.memtrack_live_count = 0;

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
        setenv("VIEW_MEM_STAGES", "1", 1);
        execl(LAMBDA_EXE, LAMBDA_EXE, "view", info.html_path.c_str(), "--headless", "--no-log", (char*)NULL);
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
        tv.tv_sec = PAGE_TIMEOUT_SECONDS;
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
    struct rusage child_usage;
    memset(&child_usage, 0, sizeof(child_usage));
    wait4(pid, &status, 0, &child_usage);
    result.peak_rss_bytes = RUSAGE_MAXRSS_TO_BYTES(child_usage.ru_maxrss);

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

    result.layout_ms = parse_prof_ms(result.output, "[LAYOUT_PROF]");
    result.render_ms = parse_prof_ms(result.output, "[RENDER_PROF]");
    result.baseline_footprint_bytes = parse_before_load_footprint(result.output);
    result.current_footprint_bytes = parse_after_cleanup_footprint(result.output);
    result.peak_footprint_bytes = parse_peak_footprint(result.output);
    result.memtrack_live_seen = parse_memtrack_live_seen(result.output);
    result.memtrack_live_bytes = parse_memtrack_live_bytes(result.output);
    result.memtrack_live_count = parse_memtrack_live_count(result.output);
    return result;
}

#endif

// ============================================================================
// Parallel execution
// ============================================================================

static std::vector<PageTestResult> g_page_results;

static void run_page_tests_parallel(int jobs) {
    size_t n = g_page_tests.size();
    g_page_results.resize(n);
    if (n == 0) return;

    int num_threads = std::min(jobs, (int)n);
    std::atomic<size_t> next_idx{0};
    std::atomic<size_t> completed{0};
    std::mutex progress_mutex;

    auto worker = [&]() {
        while (true) {
            size_t idx = next_idx.fetch_add(1);
            if (idx >= n) break;
            g_page_results[idx] = run_page_test(g_page_tests[idx]);

            const PageTestResult& result = g_page_results[idx];
            size_t done = completed.fetch_add(1) + 1;

            std::lock_guard<std::mutex> lock(progress_mutex);
            std::cout << "  [" << done << "/" << n << "] "
                      << g_page_tests[idx].test_name
                      << " finished in " << (int)result.elapsed_ms << "ms";
            if (result.layout_ms >= 0 || result.render_ms >= 0) {
                std::cout << " (layout="
                          << (int)(result.layout_ms >= 0 ? result.layout_ms : 0.0)
                          << "ms, render="
                          << (int)(result.render_ms >= 0 ? result.render_ms : 0.0)
                          << "ms)";
            }
            if (result.timed_out) {
                std::cout << " (TIMEOUT)";
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

class PageLoadTest : public ::testing::TestWithParam<size_t> {
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
        if (g_page_tests.empty()) {
            GTEST_SKIP() << "No page tests found in " PAGE_DIR;
        }
    }
};

TEST_P(PageLoadTest, LoadWithoutCrash) {
    size_t idx = GetParam();
    if (idx >= g_page_tests.size()) {
        GTEST_SKIP() << "No test at index " << idx;
    }
    const PageTestInfo& info = g_page_tests[idx];
    const PageTestResult& result = g_page_results[idx];

    SCOPED_TRACE("Page: " + info.test_name);

    // Print timing
    uint64_t page_footprint_bytes = result.current_footprint_bytes > 0
        ? result.current_footprint_bytes
        : result.peak_footprint_bytes;
    uint64_t footprint_delta_bytes =
        (result.baseline_footprint_bytes > 0 && page_footprint_bytes >= result.baseline_footprint_bytes)
            ? page_footprint_bytes - result.baseline_footprint_bytes
            : 0;

    char timing_buf[260];
    snprintf(timing_buf, sizeof(timing_buf), "  [%s] %dms (layout=%.0fms, render=%.0fms, rss=%lluMB, footprint=%lluMB, peak=%lluMB, page_delta=%lluMB, live=%lluKB/%llu)%s",
             info.test_name.c_str(), (int)result.elapsed_ms,
             result.layout_ms >= 0 ? result.layout_ms : 0.0,
             result.render_ms >= 0 ? result.render_ms : 0.0,
             (unsigned long long)(result.peak_rss_bytes / (1024 * 1024)),
             (unsigned long long)(page_footprint_bytes / (1024 * 1024)),
             (unsigned long long)(result.peak_footprint_bytes / (1024 * 1024)),
             (unsigned long long)(footprint_delta_bytes / (1024 * 1024)),
             (unsigned long long)(result.memtrack_live_bytes / 1024),
             (unsigned long long)result.memtrack_live_count,
             result.timed_out ? " (TIMEOUT)" : "");
    std::cout << timing_buf << std::endl;

    // Print output on failure
    if (result.exit_code != 0) {
        std::cerr << "\n--- Output for " << info.test_name << " ---\n"
                  << result.output
                  << "--- End output ---\n";
    }

    EXPECT_FALSE(result.timed_out)
        << info.test_name << " timed out after " << PAGE_TIMEOUT_SECONDS << "s";

    EXPECT_EQ(result.exit_code, 0)
        << info.test_name << " exited with code " << result.exit_code
        << (result.exit_code > 128 ? " (signal " + std::to_string(result.exit_code - 128) + ")" : "");

    // Layout time limit
    if (result.layout_ms >= 0) {
        EXPECT_LT(result.layout_ms, MAX_LAYOUT_SECONDS * 1000)
            << info.test_name << " layout took " << result.layout_ms << "ms (limit: "
            << (MAX_LAYOUT_SECONDS * 1000) << "ms)";
    }

    // Render time limit
    if (result.render_ms >= 0) {
        EXPECT_LT(result.render_ms, MAX_RENDER_SECONDS * 1000)
            << info.test_name << " render took " << result.render_ms << "ms (limit: "
            << (MAX_RENDER_SECONDS * 1000) << "ms)";
    }

    // Memory limit. Prefer Lambda-owned live allocation bytes from memtrack at
    // shutdown. macOS phys_footprint includes allocator/framework resident caches
    // that are not live Lambda allocations and may not return to the OS before
    // process exit. Fall back to the older footprint/RSS caps when memtrack
    // telemetry is unavailable.
    if (result.memtrack_live_seen) {
        EXPECT_LE(result.memtrack_live_bytes, MAX_PAGE_MEMTRACK_LIVE_BYTES)
            << info.test_name << " Lambda-owned live memory = "
            << (result.memtrack_live_bytes / 1024) << " KB in "
            << result.memtrack_live_count << " allocations (limit: "
            << (MAX_PAGE_MEMTRACK_LIVE_BYTES / (1024 * 1024)) << " MB)";
    } else if (page_footprint_bytes > 0) {
        if (footprint_delta_bytes > 0) {
            EXPECT_LE(footprint_delta_bytes, MAX_PAGE_FOOTPRINT_DELTA_BYTES)
                << info.test_name << " page phys_footprint growth = "
                << (footprint_delta_bytes / (1024 * 1024)) << " MB (limit: "
                << (MAX_PAGE_FOOTPRINT_DELTA_BYTES / (1024 * 1024)) << " MB, baseline: "
                << (result.baseline_footprint_bytes / (1024 * 1024)) << " MB, retained: "
                << (page_footprint_bytes / (1024 * 1024)) << " MB, peak: "
                << (result.peak_footprint_bytes / (1024 * 1024)) << " MB)";
        } else {
            EXPECT_LE(page_footprint_bytes, MAX_PEAK_FOOTPRINT_BYTES)
                << info.test_name << " peak phys_footprint = "
                << (page_footprint_bytes / (1024 * 1024)) << " MB (limit: "
                << (MAX_PEAK_FOOTPRINT_BYTES / (1024 * 1024)) << " MB)";
        }
    } else if (result.peak_rss_bytes > 0) {
        EXPECT_LE(result.peak_rss_bytes, MAX_PEAK_RSS_BYTES)
            << info.test_name << " peak RSS = "
            << (result.peak_rss_bytes / (1024 * 1024)) << " MB (limit: "
            << (MAX_PEAK_RSS_BYTES / (1024 * 1024)) << " MB)";
    }
}

INSTANTIATE_TEST_SUITE_P(
    PageLoad,
    PageLoadTest,
    ::testing::Range(size_t(0), g_page_tests.size()),
    [](const ::testing::TestParamInfo<size_t>& info) {
        if (info.param < g_page_tests.size()) {
            std::string name = g_page_tests[info.param].test_name;
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

    // Parse -j N for parallelism (default: CPU count - 1, minimum 1).
    int jobs = (int)std::thread::hardware_concurrency() - 1;
    if (jobs <= 0) jobs = 1;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--jobs") == 0) && i + 1 < argc) {
            jobs = atoi(argv[++i]);
            if (jobs <= 0) jobs = 1;
        }
    }

    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Page Load (Headless) Test Suite                      ║\n";
    std::cout << "║                                                          ║\n";
    std::cout << "║  Runs HTML pages from test/layout/data/page/ and         ║\n";
    std::cout << "║  test/layout/data/markdown/ via:                         ║\n";
    std::cout << "║    ./lambda.exe view <html> --headless --no-log          ║\n";
    std::cout << "║  Verifies pages load without crashing and within         ║\n";
    std::cout << "║  time and page memory-growth limits.                     ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    if (g_page_tests.empty()) {
        std::cerr << "WARNING: No HTML files found in " PAGE_DIR "\n\n";
    } else {
        std::cout << "Found " << g_page_tests.size() << " page(s), running with "
                  << jobs << " parallel job(s):\n";
        for (const auto& t : g_page_tests) {
            std::cout << "  • " << t.test_name << "\n";
        }
        std::cout << "\n";

        // Run all tests in parallel before GTest checks results
        auto t0 = std::chrono::steady_clock::now();
        run_page_tests_parallel(jobs);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "All " << g_page_tests.size() << " pages loaded in "
                  << ms << " ms (" << jobs << " parallel jobs)\n\n";
    }

    return RUN_ALL_TESTS();
}
