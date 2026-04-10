#ifndef TEST_LAMBDA_HELPERS_HPP
#define TEST_LAMBDA_HELPERS_HPP

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <unordered_map>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #define NOHELP
    #define NOMCX
    #include <windows.h>
    #include <process.h>
    #include <io.h>
    #include <direct.h>
    #define getcwd _getcwd
    #define chdir _chdir
    #define unlink _unlink
    #define access _access
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
    #define F_OK 0
    #define LAMBDA_EXE "lambda.exe"
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

// Structure to hold test script info
struct LambdaTestInfo {
    std::string script_path;
    std::string expected_path;
    std::string test_name;
    bool is_procedural;  // true for procedural scripts (run with "lambda.exe run")

    // For Google Test parameterized test naming
    friend std::ostream& operator<<(std::ostream& os, const LambdaTestInfo& info) {
        return os << info.test_name;
    }
};

// Returns a platform-specific expected output path if one exists, otherwise the
// generic .txt path. Checks for .win.txt on Windows, .linux.txt on Linux and .mac.txt on macOS.
inline std::string platform_expected_path(const std::string& base_txt_path) {
#if defined(_WIN32)
    std::string win_path = base_txt_path.substr(0, base_txt_path.length() - 4) + ".win.txt";
    if (access(win_path.c_str(), F_OK) == 0) return win_path;
#elif defined(__linux__)
    std::string linux_path = base_txt_path.substr(0, base_txt_path.length() - 4) + ".linux.txt";
    if (access(linux_path.c_str(), F_OK) == 0) return linux_path;
#elif defined(__APPLE__)
    std::string mac_path = base_txt_path.substr(0, base_txt_path.length() - 4) + ".mac.txt";
    if (access(mac_path.c_str(), F_OK) == 0) return mac_path;
#endif
    return base_txt_path;
}

// ============================================================================
// Slow benchmark tests excluded from baseline (each takes >1s in debug build).
// Run individually with: ./test/test_lambda_gtest.exe --gtest_filter=*awfy_cd*
// ============================================================================
static const char* SLOW_BENCHMARK_TESTS[] = {
    // awfy — timeouts or multi-second in debug build
    "awfy_cd",              // ~15s+ (timeout)
    "awfy_cd2",             // ~15s+ (timeout)
    "awfy_havlak",          // ~9s
    "awfy_havlak2",         // ~9s
    "awfy_mandelbrot",      // ~15s+ (timeout)
    "awfy_mandelbrot2",     // ~15s+ (timeout)
    "awfy_nbody",           // ~2s
    "awfy_nbody2",          // ~2s
    // r7rs — compute-heavy benchmarks
    "r7rs_ack",             // ~6s
    "r7rs_fft",             // ~1s
    "r7rs_fft2",            // ~1s
    "r7rs_fib",             // ~1.3s
    "r7rs_fib2",            // ~1.3s
    "r7rs_fibfp",           // ~4s
    "r7rs_fibfp2",          // ~2.5s
    "r7rs_mbrot",           // ~7s
    "r7rs_sum",             // ~4s
    "r7rs_sumfp",           // ~1s
    // beng
    "beng_binarytrees",     // ~2s
    "beng_mandelbrot",      // ~15s+ (timeout)
    "beng_nbody",           // ~2.4s
    "beng_spectralnorm",    // ~15s+ (timeout)
    // beng — slow in MIR Direct debug build
    "beng_knucleotide",     // ~60s+ (timeout in MIR Direct)
    // kostya
    "kostya_base64",        // ~15s+ (timeout)
    "kostya_brainfuck",     // ~15s+ (timeout)
    "kostya_collatz",       // ~15s+ (timeout)
    "kostya_json_gen",      // ~4s
    "kostya_levenshtein",   // ~8s
    "kostya_matmul",        // ~15s+ (timeout)
    "kostya_primes",        // ~9s
    // larceny
    "larceny_array1",       // ~6s
    "larceny_deriv",        // ~3s
    "larceny_deriv2",       // ~2s
    "larceny_diviter",      // ~15s+ (timeout)
    "larceny_divrec",       // ~2s
    "larceny_gcbench",      // ~15s+ (timeout)
    "larceny_gcbench2",     // ~15s+ (timeout)
    "larceny_pnpoly",       // ~15s+ (timeout)
    "larceny_puzzle",       // ~13s
    "larceny_quicksort",    // ~3s
    "larceny_ray",          // ~6s
    "larceny_triangl",      // ~15s+ (timeout)
};
static const size_t NUM_SLOW_BENCHMARK_TESTS = sizeof(SLOW_BENCHMARK_TESTS) / sizeof(SLOW_BENCHMARK_TESTS[0]);

inline bool is_slow_benchmark(const std::string& test_name) {
    for (size_t i = 0; i < NUM_SLOW_BENCHMARK_TESTS; i++) {
        if (test_name == SLOW_BENCHMARK_TESTS[i]) return true;
    }
    return false;
}

// Helper function to execute a lambda script and capture output
// is_procedural: if true, uses "./lambda.exe run <script>" for procedural scripts
// use_mir: if true, uses MIR Direct (default JIT path); if false, uses C2MIR (--c2mir flag)
inline char* execute_lambda_script(const char* script_path, bool is_procedural = false, bool use_mir = false) {
    char command[512];
    const char* c2mir_flag = use_mir ? "" : " --c2mir";
    const char* exe = use_mir ? "lambda.exe" : "lambda-jube.exe";
    const char* no_log_flag = " --no-log";  // always disable logging in tests for performance
#ifdef _WIN32
    if (is_procedural) {
        snprintf(command, sizeof(command), "%s run%s%s \"%s\"", exe, no_log_flag, c2mir_flag, script_path);
    } else {
        snprintf(command, sizeof(command), "%s%s%s \"%s\"", exe, no_log_flag, c2mir_flag, script_path);
    }
#else
    if (is_procedural) {
        snprintf(command, sizeof(command), "./%s run%s%s \"%s\"", exe, no_log_flag, c2mir_flag, script_path);
    } else {
        snprintf(command, sizeof(command), "./%s%s%s \"%s\"", exe, no_log_flag, c2mir_flag, script_path);
    }
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error: Could not execute command: %s\n", command);
        return nullptr;
    }

    // Read output in chunks
    char buffer[1024];
    size_t total_size = 0;
    char* full_output = nullptr;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        char* new_output = (char*)realloc(full_output, total_size + len + 1);
        if (!new_output) {
            free(full_output);
            pclose(pipe);
            return nullptr;
        }
        full_output = new_output;
        strcpy(full_output + total_size, buffer);
        total_size += len;
    }

    int exit_code = pclose(pipe);
    if (WEXITSTATUS(exit_code) != 0) {
        fprintf(stderr, "Error: lambda.exe exited with code %d for script: %s\n",
                WEXITSTATUS(exit_code), script_path);
        free(full_output);
        return nullptr;
    }

    // If no output was collected, return empty string
    if (!full_output) {
        return strdup("");
    }

    // Extract result from "##### Script" marker
    char* marker = strstr(full_output, "##### Script");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++; // Skip the newline
            // Create a copy of the result
            char* result = strdup(result_start);
            free(full_output);
            return result;
        }
    }

    return full_output;
}

// Helper function to trim trailing whitespace
inline void trim_trailing_whitespace(char* str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Helper function to check if a file exists
inline bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

// Helper function to get test name from script path
// Includes parent directory prefix to avoid name collisions across directories
inline std::string get_test_name(const std::string& script_path) {
    // Extract directory name and filename
    size_t last_slash = script_path.find_last_of("/\\");
    std::string filename = (last_slash != std::string::npos)
        ? script_path.substr(last_slash + 1)
        : script_path;

    // Check if there's a parent directory to use as prefix
    std::string prefix;
    if (last_slash != std::string::npos && last_slash > 0) {
        size_t prev_slash = script_path.find_last_of("/\\", last_slash - 1);
        std::string dir_name = (prev_slash != std::string::npos)
            ? script_path.substr(prev_slash + 1, last_slash - prev_slash - 1)
            : script_path.substr(0, last_slash);
        // Only add prefix for subdirectories (not for the base "lambda" directory)
        if (dir_name != "lambda") {
            prefix = dir_name + "_";
        }
    }

    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        filename = filename.substr(0, dot_pos);
    }

    std::string test_name = prefix + filename;

    // Replace invalid characters for test names
    for (char& c : test_name) {
        if (!isalnum(c) && c != '_') {
            c = '_';
        }
    }

    return test_name;
}

// Discover all .ls files with matching .txt files in a directory
inline std::vector<LambdaTestInfo> discover_tests_in_directory(const char* dir_path, bool is_procedural = false) {
    std::vector<LambdaTestInfo> tests;

#ifdef _WIN32
    std::string search_path = std::string(dir_path) + "\\*.ls";
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path.c_str(), &find_data);

    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            std::string filename = find_data.cFileName;
            std::string script_path = std::string(dir_path) + "/" + filename;

            // Build expected output path (.ls -> .txt), with platform override
            std::string base_txt = script_path;
            size_t dot_pos = base_txt.find_last_of('.');
            if (dot_pos != std::string::npos) {
                base_txt = base_txt.substr(0, dot_pos) + ".txt";
            }
            std::string expected_path = platform_expected_path(base_txt);

            // Only add if matching .txt file exists
            if (file_exists(expected_path)) {
                LambdaTestInfo info;
                info.script_path = script_path;
                info.expected_path = expected_path;
                info.test_name = get_test_name(script_path);
                info.is_procedural = is_procedural;
                tests.push_back(info);
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
#else
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return tests;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Check if it's a .ls file
        if (filename.length() > 3 && filename.substr(filename.length() - 3) == ".ls") {
            std::string script_path = std::string(dir_path) + "/" + filename;

            // Build expected output path (.ls -> .txt), with platform override
            std::string base_txt = script_path;
            size_t dot_pos = base_txt.find_last_of('.');
            if (dot_pos != std::string::npos) {
                base_txt = base_txt.substr(0, dot_pos) + ".txt";
            }
            std::string expected_path = platform_expected_path(base_txt);

            // Only add if matching .txt file exists
            if (file_exists(expected_path)) {
                LambdaTestInfo info;
                info.script_path = script_path;
                info.expected_path = expected_path;
                info.test_name = get_test_name(script_path);
                info.is_procedural = is_procedural;
                tests.push_back(info);
            }
        }
    }
    closedir(dir);
#endif

    // Sort tests by name for consistent ordering
    std::sort(tests.begin(), tests.end(), [](const LambdaTestInfo& a, const LambdaTestInfo& b) {
        return a.test_name < b.test_name;
    });

    return tests;
}

// Helper function to read expected output from file
inline char* read_expected_output(const char* expected_file_path) {
    FILE* file = fopen(expected_file_path, "r");
    if (!file) {
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return nullptr;
    }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    trim_trailing_whitespace(content);
    return content;
}

// Helper function to strip __TIMING__ lines from output (benchmark instrumentation)
inline void strip_timing_lines(char* output) {
    if (!output) return;
    char* read = output;
    char* write = output;
    while (*read) {
        // check if current line starts with __TIMING__:
        if (strncmp(read, "__TIMING__:", 11) == 0) {
            // skip this entire line
            while (*read && *read != '\n') read++;
            if (*read == '\n') read++;
            continue;
        }
        // copy this line
        while (*read && *read != '\n') {
            *write++ = *read++;
        }
        if (*read == '\n') {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

// Helper function to test lambda script against expected output file
inline void test_lambda_script_against_file(const char* script_path, const char* expected_file_path, bool is_procedural, bool use_mir = false) {
    // use_mir=false: runs C2MIR path (--c2mir flag), use_mir=true: runs MIR Direct (default)
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_lambda_script(script_path, is_procedural, use_mir);
    ASSERT_NE(actual_output, nullptr) << "Could not execute lambda script: " << script_path;

    // Trim whitespace from actual output
    trim_trailing_whitespace(actual_output);

    // Strip __TIMING__ lines (benchmark instrumentation — variable across runs)
    strip_timing_lines(actual_output);
    trim_trailing_whitespace(actual_output);

    // Compare outputs
    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for script: " << script_path
        << " (expected " << strlen(expected_output) << " chars, got " << strlen(actual_output) << " chars)";

    free(expected_output);
    free(actual_output);
}

// ============================================================================
// Batch Execution Support
//
// Runs all test scripts in a single lambda.exe process via "test-batch"
// command, eliminating per-test process spawn overhead.
// ============================================================================

#include <unordered_map>

struct BatchResult {
    std::string output;
    int status;
};

// Extract output after "##### Script" marker (if present), same logic as execute_lambda_script
inline char* extract_script_output(const std::string& raw_output) {
    char* full = strdup(raw_output.c_str());
    if (!full) return strdup("");

    char* marker = strstr(full, "##### Script");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++;
            char* result = strdup(result_start);
            free(full);
            return result;
        }
    }
    return full;
}

// Run a single sub-batch of scripts via test-batch command.
// Appends results to the provided map.
inline void run_sub_batch(
    const std::vector<std::string>& scripts,
    const std::vector<bool>& is_procedural,
    size_t start, size_t end,
    bool use_mir,
    int batch_id,
    std::unordered_map<std::string, BatchResult>& results)
{
    // Write manifest for this chunk (include PID to avoid race between parallel shards)
    char manifest_path[128];
    snprintf(manifest_path, sizeof(manifest_path), "./temp/batch_%d_%d.txt", (int)getpid(), batch_id);
    FILE* manifest = fopen(manifest_path, "w");
    if (!manifest) return;

    for (size_t i = start; i < end; i++) {
        if (is_procedural[i]) {
            fprintf(manifest, "run %s\n", scripts[i].c_str());
        } else {
            fprintf(manifest, "%s\n", scripts[i].c_str());
        }
    }
    fclose(manifest);

    char command[512];
    const char* c2mir_flag = use_mir ? "" : " --c2mir";
    const char* exe = use_mir ? "lambda.exe" : "lambda-jube.exe";
#ifdef _WIN32
    snprintf(command, sizeof(command), "%s test-batch --no-log --timeout=60%s < \"%s\"",
             exe, c2mir_flag, manifest_path);
#else
    snprintf(command, sizeof(command), "./%s test-batch --no-log --timeout=60%s < \"%s\"",
             exe, c2mir_flag, manifest_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) return;

    char buffer[4096];
    std::string current_script;
    std::string current_output;
    bool in_script = false;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        if (buffer[0] == '\x01') {
            if (strncmp(buffer + 1, "BATCH_START ", 12) == 0) {
                current_script = std::string(buffer + 13);
                while (!current_script.empty() &&
                       (current_script.back() == '\n' || current_script.back() == '\r'))
                    current_script.pop_back();
                current_output.clear();
                in_script = true;
            } else if (strncmp(buffer + 1, "BATCH_END ", 10) == 0) {
                int status = atoi(buffer + 11);
                results[current_script] = {current_output, status};
                in_script = false;
            }
        } else if (in_script) {
            current_output += buffer;
        }
    }

    pclose(pipe);
    unlink(manifest_path);
}

// Max scripts per lambda.exe process to avoid state accumulation crashes
static const size_t BATCH_CHUNK_SIZE = 50;

// Max parallel sub-batch processes to avoid resource exhaustion
static const size_t MAX_PARALLEL_LAMBDA_BATCHES = 8;

// Run multiple scripts using test-batch command, splitting into sub-batches
// to avoid memory/state accumulation in the lambda.exe process.
// Sub-batches run in parallel threads for faster execution.
// Returns a map from script_path -> BatchResult.
inline std::unordered_map<std::string, BatchResult> execute_lambda_batch(
    const std::vector<std::string>& scripts,
    const std::vector<bool>& is_procedural,
    bool use_mir = true,
    size_t batch_chunk_size = BATCH_CHUNK_SIZE)
{
    std::unordered_map<std::string, BatchResult> results;
    if (scripts.empty()) return results;

    // Build list of sub-batch ranges
    struct SubBatch { size_t start; size_t end; int id; };
    std::vector<SubBatch> batches;
    int batch_id = 0;
    for (size_t start = 0; start < scripts.size(); start += batch_chunk_size) {
        size_t end = std::min(start + batch_chunk_size, scripts.size());
        batches.push_back({start, end, batch_id++});
    }

    // Run sub-batches in parallel with limited concurrency (work-stealing pattern)
    std::vector<std::unordered_map<std::string, BatchResult>> thread_results(batches.size());
    std::atomic<size_t> next_batch{0};
    size_t num_workers = std::min(MAX_PARALLEL_LAMBDA_BATCHES, batches.size());
    std::vector<std::thread> threads;
    for (size_t w = 0; w < num_workers; w++) {
        threads.emplace_back([&]() {
            while (true) {
                size_t i = next_batch.fetch_add(1, std::memory_order_relaxed);
                if (i >= batches.size()) break;
                run_sub_batch(scripts, is_procedural,
                              batches[i].start, batches[i].end,
                              use_mir, batches[i].id, thread_results[i]);
            }
        });
    }
    for (auto& t : threads) t.join();

    // Merge results
    for (auto& partial : thread_results) {
        for (auto& kv : partial) {
            results[kv.first] = std::move(kv.second);
        }
    }

    return results;
}

#endif // TEST_LAMBDA_HELPERS_HPP
