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
#include <mutex>
#include <unordered_map>
#include "../lambda/runtime/compiler_timing.hpp"

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
    #include <time.h>
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
inline char* execute_lambda_script(const char* script_path, bool is_procedural = false) {
    char command[512];
    const char* exe = "lambda.exe";
    const char* no_log_flag = " --no-log";  // always disable logging in tests for performance
#ifdef _WIN32
    if (is_procedural) {
        snprintf(command, sizeof(command), "%s run%s \"%s\"", exe, no_log_flag, script_path);
    } else {
        snprintf(command, sizeof(command), "%s%s \"%s\"", exe, no_log_flag, script_path);
    }
#else
    if (is_procedural) {
        snprintf(command, sizeof(command), "./%s run%s \"%s\"", exe, no_log_flag, script_path);
    } else {
        snprintf(command, sizeof(command), "./%s%s \"%s\"", exe, no_log_flag, script_path);
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

// remove compiler protocol records before comparing program output
inline void strip_lambda_protocol_lines(char* output) {
    if (!output) return;
    char* read = output;
    char* write = output;
    while (*read) {
        bool protocol = ((unsigned char)*read == 1) &&
            (strncmp(read + 1, "COMPILER_TIMING ", 16) == 0 ||
             strncmp(read + 1, "MIR_VOLUME ", 11) == 0);
        if (protocol) {
            while (*read && *read != '\n') read++;
            if (*read == '\n') read++;
            continue;
        }
        while (*read && *read != '\n') *write++ = *read++;
        if (*read == '\n') *write++ = *read++;
    }
    *write = '\0';
}

// Helper function to test lambda script against expected output file
inline void test_lambda_script_against_file(const char* script_path, const char* expected_file_path, bool is_procedural) {
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_lambda_script(script_path, is_procedural);
    ASSERT_NE(actual_output, nullptr) << "Could not execute lambda script: " << script_path;

    // Trim whitespace from actual output
    trim_trailing_whitespace(actual_output);

    // Strip __TIMING__ lines (benchmark instrumentation — variable across runs)
    strip_timing_lines(actual_output);
    strip_lambda_protocol_lines(actual_output);
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
    long long elapsed_us;
    LambdaCompilerTiming timing;
    bool has_timing;
    bool has_volume;
};

inline bool parse_lambda_timing_line(const char* line, LambdaCompilerTiming* out) {
    if (!line || !out) return false;
    unsigned long long parse_us = 0, ast_build_us = 0, mir_lower_us = 0;
    unsigned long long module_finalize_us = 0, link_us = 0, build_transpile_us = 0;
    int schema = 0;
    int matched = sscanf(line,
        "COMPILER_TIMING schema=%d parse_us=%llu ast_build_us=%llu "
        "mir_lower_us=%llu module_finalize_us=%llu link_us=%llu "
        "build_transpile_us=%llu",
        &schema, &parse_us, &ast_build_us, &mir_lower_us,
        &module_finalize_us, &link_us, &build_transpile_us);
    if (matched != 7 || schema != 1) return false;
    out->parse_us = parse_us;
    out->ast_build_us = ast_build_us;
    out->mir_lower_us = mir_lower_us;
    out->module_finalize_us = module_finalize_us;
    out->link_us = link_us;
    out->build_transpile_us = build_transpile_us;
    out->valid = 1;
    return true;
}

inline bool parse_lambda_volume_line(const char* line, LambdaCompilerTiming* out) {
    if (!line || !out) return false;
    unsigned long long modules = 0, functions = 0, insns = 0;
    int schema = 0;
    char sample_id[1024], test_name[256];
    int matched = sscanf(line, "MIR_VOLUME schema=%d sample_id=%1023s test_name=%255s modules=%llu functions=%llu insns=%llu",
                         &schema, sample_id, test_name, &modules, &functions, &insns);
    if (matched != 6) {
        matched = sscanf(line, "MIR_VOLUME schema=%d modules=%llu functions=%llu insns=%llu",
                         &schema, &modules, &functions, &insns);
    }
    if ((matched != 6 && matched != 4) || schema != 1) return false;
    out->mir_module_count = modules;
    out->mir_function_count = functions;
    out->mir_insn_count = insns;
    return true;
}

inline long long lambda_test_now_us() {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (long long)((counter.QuadPart * 1000000LL) / frequency.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + (long long)(ts.tv_nsec / 1000);
#endif
}

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
    int batch_id,
    std::unordered_map<std::string, BatchResult>& results,
    std::atomic<size_t>& completed_scripts,
    std::mutex& progress_mutex,
    size_t total_scripts)
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
    const char* exe = "lambda.exe";
#ifdef _WIN32
    snprintf(command, sizeof(command), "%s test-batch --no-log --timeout=60 < \"%s\"",
             exe, manifest_path);
#else
    snprintf(command, sizeof(command), "./%s test-batch --no-log --timeout=60 < \"%s\"",
             exe, manifest_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) return;

    char buffer[4096];
    std::string current_script;
    std::string current_output;
    bool in_script = false;
    long long current_start_us = 0;
    LambdaCompilerTiming current_timing = {};
    bool current_has_timing = false;
    bool current_has_volume = false;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        // A failed script may leave stdout without a newline. In that case the
        // control marker shares the output line and must still close the result.
        char* control = strchr(buffer, '\x01');
        bool is_start = control && strncmp(control + 1, "BATCH_START ", 12) == 0;
        bool is_end = control && strncmp(control + 1, "BATCH_END ", 10) == 0;
        bool is_timing = control && strncmp(control + 1, "COMPILER_TIMING ", 16) == 0;
        bool is_volume = control && strncmp(control + 1, "MIR_VOLUME ", 11) == 0;
        if (is_start || is_end || is_timing || is_volume) {
            if (in_script && control > buffer) {
                current_output.append(buffer, (size_t)(control - buffer));
            }
            if (is_start) {
                current_script = std::string(control + 13);
                while (!current_script.empty() &&
                       (current_script.back() == '\n' || current_script.back() == '\r'))
                    current_script.pop_back();
                current_output.clear();
                in_script = true;
                current_start_us = lambda_test_now_us();
                current_timing = {};
                current_has_timing = false;
                current_has_volume = false;
            } else if (is_timing) {
                current_has_timing = parse_lambda_timing_line(control + 1, &current_timing) ||
                    current_has_timing;
            } else if (is_volume) {
                current_has_volume = parse_lambda_volume_line(control + 1, &current_timing) ||
                    current_has_volume;
            } else {
                int status = atoi(control + 11);
                long long elapsed_us = current_start_us > 0 ? lambda_test_now_us() - current_start_us : 0;
                BatchResult result = {current_output, status, elapsed_us,
                                      current_timing, current_has_timing,
                                      current_has_volume};
                results[current_script] = result;
                size_t done = completed_scripts.fetch_add(1, std::memory_order_relaxed) + 1;
                {
                    std::lock_guard<std::mutex> lock(progress_mutex);
                    printf("  [batch %d] [%zu/%zu] %s finished in %.3fs",
                           batch_id, done, total_scripts, current_script.c_str(),
                           (double)elapsed_us / 1000000.0);
                    if (status != 0) {
                        printf(" (status=%d)", status);
                    }
                    printf("\n");
                    fflush(stdout);
                }
                in_script = false;
            }
        } else if (in_script) {
            current_output += buffer;
        }
    }

    pclose(pipe);
    unlink(manifest_path);
}

// Max scripts per lambda.exe process for ordinary regression runs.
static const size_t BATCH_CHUNK_SIZE = 50;

// this helper is shared by multiple test translation units; external inline
// linkage avoids one unused private copy becoming a -Werror failure.
inline size_t lambda_capture_batch_chunk_size() {
    const char* override_value = getenv("AST_TUNE_BATCH_CHUNK");
    if (override_value && *override_value) {
        char* end = nullptr;
        unsigned long parsed = strtoul(override_value, &end, 10);
        if (end != override_value && *end == '\0' && parsed > 0) {
            return (size_t)parsed;
        }
    }
    // Timing runs may override this explicitly; otherwise retain the normal
    // 50-source worker so compiler totals stay comparable with the baseline.
    return BATCH_CHUNK_SIZE;
}

// Max parallel sub-batch processes to avoid resource exhaustion
static const size_t MAX_PARALLEL_LAMBDA_BATCHES = 8;

// Timing captures may deliberately cap process fan-out so compiler samples
// are not lost to host-level process pressure while large AST/MIR batches run.
static size_t lambda_batch_worker_limit(size_t batch_count) {
    size_t limit = std::min(MAX_PARALLEL_LAMBDA_BATCHES, batch_count);
    const char* override_value = getenv("AST_TUNE_BATCH_WORKERS");
    if (override_value && *override_value) {
        char* end = nullptr;
        unsigned long parsed = strtoul(override_value, &end, 10);
        if (end != override_value && *end == '\0' && parsed > 0) {
            limit = std::min(limit, (size_t)parsed);
        }
    }
    return limit;
}

// Run multiple scripts using test-batch command, splitting into sub-batches
// to avoid memory/state accumulation in the lambda.exe process.
// Sub-batches run in parallel threads for faster execution.
// Returns a map from script_path -> BatchResult.
inline std::unordered_map<std::string, BatchResult> execute_lambda_batch(
    const std::vector<std::string>& scripts,
    const std::vector<bool>& is_procedural,
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
    std::atomic<size_t> completed_scripts{0};
    std::mutex progress_mutex;
    size_t num_workers = lambda_batch_worker_limit(batches.size());
    std::vector<std::thread> threads;
    for (size_t w = 0; w < num_workers; w++) {
        threads.emplace_back([&]() {
            while (true) {
                size_t i = next_batch.fetch_add(1, std::memory_order_relaxed);
                if (i >= batches.size()) break;
                run_sub_batch(scripts, is_procedural,
                              batches[i].start, batches[i].end,
                              batches[i].id, thread_results[i],
                              completed_scripts, progress_mutex, scripts.size());
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
