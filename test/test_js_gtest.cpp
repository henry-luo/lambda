#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <thread>
#include <atomic>
#include "test_process.h"
#include "test_baseline_mode.hpp"
#include "../lambda/runtime/compiler_timing.hpp"
#include "test_ast_tune_capture.hpp"

extern "C" {
#include "../lib/shell.h"
}

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
    #define LAMBDA_EXE "lambda.exe"
    #ifndef F_OK
    #define F_OK 0
    #endif
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <dirent.h>
#define LAMBDA_EXE "./lambda.exe"
#endif

static LambdaCompilerTiming g_js_direct_timing = {};
static bool g_js_direct_has_timing = false;
static bool g_js_direct_has_volume = false;
static int g_js_direct_status = 0;

enum JsExecutionBackend {
    JS_BACKEND_INHERIT,
    JS_BACKEND_AST,
    JS_BACKEND_MIR,
};

static const char* js_backend_env_value(JsExecutionBackend backend) {
    if (backend == JS_BACKEND_AST) return "ast";
    if (backend == JS_BACKEND_MIR) return "mir";
    return NULL;
}

static void parse_js_direct_protocol(const char* output) {
    g_js_direct_timing = {};
    g_js_direct_has_timing = false;
    g_js_direct_has_volume = false;
    if (!output) return;
    const char* line = output;
    while (*line) {
        const char* end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len > 0 && len < 2048 && line[0] == '\x01') {
            char record[2048];
            memcpy(record, line + 1, len - 1);
            record[len - 1] = '\0';
            int schema = 0;
            unsigned long long parse_us = 0, ast_us = 0, validate_us = 0;
            unsigned long long imports_us = 0, mir_us = 0, link_us = 0, build_us = 0;
            if (sscanf(record,
                    "COMPILER_TIMING schema=%d parse_us=%llu ast_build_us=%llu "
                    "validate_us=%llu imports_us=%llu mir_lower_us=%llu link_us=%llu "
                    "build_transpile_us=%llu", &schema, &parse_us, &ast_us,
                    &validate_us, &imports_us, &mir_us, &link_us, &build_us) == 8 &&
                    schema == 1) {
                g_js_direct_timing.parse_us = parse_us;
                g_js_direct_timing.ast_build_us = ast_us;
                g_js_direct_timing.validate_us = validate_us;
                g_js_direct_timing.analysis_us = imports_us;
                g_js_direct_timing.mir_lower_us = mir_us;
                g_js_direct_timing.link_us = link_us;
                g_js_direct_timing.build_transpile_us = build_us;
                g_js_direct_timing.valid = 1;
                g_js_direct_has_timing = true;
            } else {
                unsigned long long modules = 0, functions = 0, insns = 0;
                char sample[1024], name[256];
                if (sscanf(record,
                        "MIR_VOLUME schema=%d sample_id=%1023s test_name=%255s "
                        "modules=%llu functions=%llu insns=%llu", &schema, sample, name,
                        &modules, &functions, &insns) == 6 && schema == 1) {
                    g_js_direct_timing.mir_module_count = modules;
                    g_js_direct_timing.mir_function_count = functions;
                    g_js_direct_timing.mir_insn_count = insns;
                    g_js_direct_has_volume = true;
                }
            }
        }
        line = end ? end + 1 : line + len;
    }
}

// Helper function to execute a JavaScript file with lambda js and capture output
static char* execute_js_script_configured(const char* script_path,
        bool permission, const char* module_path,
        JsExecutionBackend backend = JS_BACKEND_INHERIT) {
    const char* args[7] = {LAMBDA_EXE, "js", script_path, NULL, NULL, NULL, NULL};
    int arg_count = 3;
    if (permission) args[arg_count++] = "--permission";
    args[arg_count++] = "--no-log";
    args[arg_count] = NULL;

    ShellEnvEntry env[3] = {};
    ShellOptions options = {};
    size_t env_count = 0;
    const char* backend_value = js_backend_env_value(backend);
    if (backend_value) {
        env[env_count].key = "JS_EXECUTION_BACKEND";
        env[env_count].value = backend_value;
        env_count++;
    }
    if (module_path && module_path[0]) {
        env[env_count].key = "JUBE_MODULE_PATH";
        env[env_count].value = module_path;
        env_count++;
    }
    if (env_count > 0) options.env = env;
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args,
        options.env ? &options : NULL);
    g_js_direct_status = shell_result.exit_code;
    char* full_output = shell_result.stdout_buf ? strdup(shell_result.stdout_buf) : strdup("");
    parse_js_direct_protocol(full_output);
    if (shell_result.exit_code != 0) {
        fprintf(stderr, "Error: lambda.exe js exited with code %d for script: %s\n",
                shell_result.exit_code, script_path);
        free(full_output);
        shell_result_free(&shell_result);
        return nullptr;
    }
    shell_result_free(&shell_result);

    // Return empty string for successful but empty output
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

char* execute_js_script(const char* script_path) {
    return execute_js_script_configured(script_path, false, NULL,
        JS_BACKEND_INHERIT);
}

int execute_js_script_status(const char* script_path, char* output, size_t output_size) {
    const char* args[] = {LAMBDA_EXE, "js", script_path, "--no-log", NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    options.timeout_ms = 5000;
    if (output && output_size > 0) output[0] = '\0';
    // Native timeout handling kills the full process group instead of relying on an external utility.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (output && output_size > 0 && shell_result.stdout_buf) {
        size_t copy_len = shell_result.stdout_len;
        if (copy_len >= output_size) copy_len = output_size - 1;
        memcpy(output, shell_result.stdout_buf, copy_len);
        output[copy_len] = '\0';
    }
    int exit_code = shell_result.exit_code;
    shell_result_free(&shell_result);
    return exit_code;
}

int execute_command_status(const char* command, char* output, size_t output_size) {
    if (output && output_size > 0) output[0] = '\0';
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return -1;
    }

    char buffer[256];
    size_t total_size = 0;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        if (output && output_size > 0 && total_size < output_size - 1) {
            size_t len = strlen(buffer);
            size_t copy_len = len;
            if (copy_len > output_size - 1 - total_size) {
                copy_len = output_size - 1 - total_size;
            }
            memcpy(output + total_size, buffer, copy_len);
            total_size += copy_len;
            output[total_size] = '\0';
        }
    }

    int status = pclose(pipe);
#ifdef _WIN32
    return status;
#else
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return status;
#endif
}

// Helper function to trim trailing whitespace
void trim_trailing_whitespace(char* str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Helper function to read expected output from file
char* read_expected_output(const char* expected_file_path) {
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

static bool write_module_source_manifest(const char* source_path,
        const char* module_name, const char* manifest_path) {
    FILE* source = fopen(source_path, "rb");
    if (!source) return false;
    if (fseek(source, 0, SEEK_END) != 0) {
        fclose(source);
        return false;
    }
    long source_size = ftell(source);
    if (source_size <= 0 || fseek(source, 0, SEEK_SET) != 0) {
        fclose(source);
        return false;
    }
    char* source_bytes = (char*)malloc((size_t)source_size);
    if (!source_bytes) {
        fclose(source);
        return false;
    }
    bool read_ok = fread(source_bytes, 1, (size_t)source_size, source) ==
        (size_t)source_size;
    fclose(source);
    if (!read_ok) {
        free(source_bytes);
        return false;
    }

    FILE* manifest = fopen(manifest_path, "wb");
    if (!manifest) {
        free(source_bytes);
        return false;
    }
    bool write_ok = fprintf(manifest, "module-source:%s:%s:%ld\n",
        module_name, source_path, source_size) > 0 &&
        fwrite(source_bytes, 1, (size_t)source_size, manifest) == (size_t)source_size;
    fclose(manifest);
    free(source_bytes);
    return write_ok;
}

// Helper function to test JavaScript script against expected output file
static void strip_js_timing_lines(char* output) {
    if (!output) return;
    char* read = output;
    char* write = output;
    while (*read) {
        bool instrumentation = strncmp(read, "JS_TRANSPILE_TIMING ", 20) == 0 ||
            strncmp(read, "JS_AST_COUNTERS ", 16) == 0 ||
            strncmp(read, "JS_MIR_VOLUME ", 14) == 0 ||
            (read[0] == '\x01' &&
             (strncmp(read + 1, "COMPILER_TIMING ", 16) == 0 ||
              strncmp(read + 1, "MIR_VOLUME ", 11) == 0));
        if (instrumentation) {
            while (*read && *read != '\n') read++;
            if (*read == '\n') read++;
            continue;
        }
        while (*read && *read != '\n') *write++ = *read++;
        if (*read == '\n') *write++ = *read++;
    }
    *write = '\0';
}

void test_js_script_against_file(const char* script_path, const char* expected_file_path) {
    // Get script name for better error messages
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_js_script(script_path);
    ASSERT_NE(actual_output, nullptr) << "Could not execute JavaScript script: " << script_path;

    strip_js_timing_lines(actual_output);
    // Trim whitespace from actual output
    trim_trailing_whitespace(actual_output);

    // Compare outputs
    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for JavaScript script: " << script_path
        << "\nExpected (" << strlen(expected_output) << " chars): " << expected_output
        << "\nActual (" << strlen(actual_output) << " chars): " << actual_output;

    free(expected_output);
    free(actual_output);
}

// Helper function to test JavaScript command interface
char* execute_js_builtin_tests() {
    const char* args[] = {LAMBDA_EXE, "js", "--no-log", NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (shell_result.exit_code != 0) {
        shell_result_free(&shell_result);
        return nullptr;
    }
    char* full_output = shell_result.stdout_buf ? strdup(shell_result.stdout_buf) : strdup("");
    parse_js_direct_protocol(full_output);
    shell_result_free(&shell_result);
    return full_output;
}

// Helper function to execute a JavaScript file with --document flag and capture output
char* execute_js_script_with_doc(const char* script_path, const char* html_path,
        JsExecutionBackend backend = JS_BACKEND_INHERIT) {
    const char* args[] = {
        LAMBDA_EXE, "js", script_path, "--document", html_path, "--no-log", NULL,
    };
    ShellEnvEntry env[2] = {};
    ShellOptions options = {};
    const char* backend_value = js_backend_env_value(backend);
    if (backend_value) {
        env[0].key = "JS_EXECUTION_BACKEND";
        env[0].value = backend_value;
        options.env = env;
    }
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args,
        options.env ? &options : NULL);
    g_js_direct_status = shell_result.exit_code;
    char* full_output = shell_result.stdout_buf ? strdup(shell_result.stdout_buf) : strdup("");
    parse_js_direct_protocol(full_output);
    if (shell_result.exit_code != 0) {
        fprintf(stderr, "Error: lambda.exe js exited with code %d for script: %s --document %s\n",
                shell_result.exit_code, script_path, html_path);
        free(full_output);
        shell_result_free(&shell_result);
        return nullptr;
    }
    shell_result_free(&shell_result);

    // Extract result from "##### Script" marker (same as Lambda tests)
    if (!full_output) {
        return nullptr;
    }
    char* marker = strstr(full_output, "##### Script");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++; // Skip the newline
            char* result = strdup(result_start);
            free(full_output);
            return result;
        }
    }

    return full_output;
}

// Helper function to test JavaScript DOM script against expected output file
void test_js_dom_script_against_file(const char* script_path, const char* html_path,
        const char* expected_file_path,
        JsExecutionBackend backend = JS_BACKEND_INHERIT) {
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_js_script_with_doc(script_path, html_path, backend);
    ast_tune_append_timing_row("js", script_path, script_name, g_js_direct_status,
        &g_js_direct_timing, g_js_direct_has_timing, g_js_direct_has_volume);
    ASSERT_NE(actual_output, nullptr) << "Could not execute JavaScript DOM script: " << script_path;

    strip_js_timing_lines(actual_output);
    trim_trailing_whitespace(actual_output);

    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for JavaScript DOM script: " << script_path
        << "\nExpected (" << strlen(expected_output) << " chars): " << expected_output
        << "\nActual (" << strlen(actual_output) << " chars): " << actual_output;

    free(expected_output);
    free(actual_output);
}

// ---------------------------------------------------------------------------
// Batch mode infrastructure for js-test-batch
// ---------------------------------------------------------------------------

struct JsBatchResult {
    std::string output;
    int status;
    LambdaCompilerTiming timing;
    bool has_timing;
    bool has_volume;
};

static bool parse_js_timing_line(const char* line, LambdaCompilerTiming* out) {
    if (!line || !out) return false;
    unsigned long long parse_us = 0, ast_build_us = 0, validate_us = 0;
    unsigned long long imports_us = 0, mir_lower_us = 0, link_us = 0;
    unsigned long long build_transpile_us = 0;
    int schema = 0;
    int matched = sscanf(line,
        "COMPILER_TIMING schema=%d parse_us=%llu ast_build_us=%llu "
        "validate_us=%llu imports_us=%llu mir_lower_us=%llu link_us=%llu "
        "build_transpile_us=%llu",
        &schema, &parse_us, &ast_build_us, &validate_us, &imports_us,
        &mir_lower_us, &link_us, &build_transpile_us);
    if (matched != 8 || schema != 1) return false;
    out->parse_us = parse_us;
    out->ast_build_us = ast_build_us;
    out->validate_us = validate_us;
    out->analysis_us = imports_us;
    out->mir_lower_us = mir_lower_us;
    out->link_us = link_us;
    out->build_transpile_us = build_transpile_us;
    out->valid = 1;
    return true;
}

static bool parse_js_volume_line(const char* line, LambdaCompilerTiming* out) {
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

// Max scripts per lambda.exe js-test-batch process
static const size_t JS_BATCH_CHUNK_SIZE = 50;
static bool js_baseline_mode = false;

static size_t js_capture_batch_chunk_size() {
    // Large-library workers retain MIR contexts until the batch watermark is
    // reclaimed. Timing captures must not lose samples to RSS exhaustion, so
    // use one source compilation per child while preserving the normal fast
    // batch size for ordinary regression runs.
    return getenv("LAMBDA_COMPILER_TIMING") ? 1 : JS_BATCH_CHUNK_SIZE;
}

static void run_js_sub_batch(
    const std::vector<std::string>& scripts,
    size_t start, size_t end,
    int batch_id,
    std::unordered_map<std::string, JsBatchResult>& results,
    JsExecutionBackend backend)
{
    // write manifest for this chunk
    char manifest_path[256];
    snprintf(manifest_path, sizeof(manifest_path), "./temp/js_batch_%d_%d.txt", (int)getpid(), batch_id);
    FILE* manifest = fopen(manifest_path, "w");
    if (!manifest) return;

    for (size_t i = start; i < end; i++) {
        fprintf(manifest, "%s\n", scripts[i].c_str());
    }
    fclose(manifest);

    const char* args[] = {LAMBDA_EXE, "js-test-batch", "--timeout=60", NULL};
    ShellOptions options = {0};
    options.stdin_path = manifest_path;
    ShellEnvEntry env[2] = {};
    const char* backend_value = js_backend_env_value(backend);
    if (backend_value) {
        env[0].key = "JS_EXECUTION_BACKEND";
        env[0].value = backend_value;
        options.env = env;
    }
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (shell_result.exit_code < 0) {
        shell_result_free(&shell_result);
        unlink(manifest_path);
        return;
    }

    char buffer[4096];
    std::string current_script;
    std::string current_output;
    bool in_script = false;
    LambdaCompilerTiming current_timing = {};
    bool current_has_timing = false;
    bool current_has_volume = false;

    TestProcessLines lines;
    test_process_lines_init(&lines, shell_result.stdout_buf, shell_result.stdout_len);
    while (test_process_next_line(&lines, buffer, sizeof(buffer))) {
        if (buffer[0] == '\x01') {
            if (strncmp(buffer + 1, "BATCH_START ", 12) == 0) {
                current_script = std::string(buffer + 13);
                while (!current_script.empty() &&
                       (current_script.back() == '\n' || current_script.back() == '\r'))
                    current_script.pop_back();
                current_output.clear();
                in_script = true;
                current_timing = {};
                current_has_timing = false;
                current_has_volume = false;
            } else if (strncmp(buffer + 1, "COMPILER_TIMING ", 16) == 0) {
                current_has_timing = parse_js_timing_line(buffer + 1, &current_timing) ||
                    current_has_timing;
            } else if (strncmp(buffer + 1, "MIR_VOLUME ", 11) == 0) {
                current_has_volume = parse_js_volume_line(buffer + 1, &current_timing) ||
                    current_has_volume;
            } else if (strncmp(buffer + 1, "BATCH_END ", 10) == 0) {
                int status = atoi(buffer + 11);
                JsBatchResult result = {current_output, status,
                                         current_timing, current_has_timing,
                                         current_has_volume};
                results[current_script] = result;
                in_script = false;
            }
        } else if (in_script) {
            current_output += buffer;
        }
    }

    shell_result_free(&shell_result);
    unlink(manifest_path);
}

static std::unordered_map<std::string, JsBatchResult> execute_js_batch(
    const std::vector<std::string>& scripts,
    size_t chunk_size = JS_BATCH_CHUNK_SIZE,
    JsExecutionBackend backend = JS_BACKEND_INHERIT)
{
    std::unordered_map<std::string, JsBatchResult> results;
    if (scripts.empty()) return results;

    // build list of sub-batch ranges
    struct SubBatch { size_t start; size_t end; int id; };
    std::vector<SubBatch> batches;
    int batch_id = 0;
    for (size_t start = 0; start < scripts.size(); start += chunk_size) {
        size_t end = std::min(start + chunk_size, scripts.size());
        batches.push_back({start, end, batch_id++});
    }

    // run sub-batches in parallel
    std::vector<std::unordered_map<std::string, JsBatchResult>> thread_results(batches.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < batches.size(); i++) {
        threads.emplace_back([&, i]() {
            run_js_sub_batch(scripts, batches[i].start, batches[i].end,
                             batches[i].id, thread_results[i], backend);
        });
    }
    for (auto& t : threads) t.join();

    // merge results
    for (auto& partial : thread_results) {
        for (auto& kv : partial) {
            results[kv.first] = std::move(kv.second);
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// Dynamic test discovery — std::string/vector used here for GTest API only
// ---------------------------------------------------------------------------

struct JsTestParam {
    std::string script_path;
    std::string expected_path;
    std::string html_path;   // non-empty → DOM test (has matching .html)
    std::string test_name;   // sanitised for GTest (alphanumeric + underscore)
    bool permission;         // true → execute with the permission sandbox enabled
    char module_path[512];   // optional isolated Jube module profile
};

static const char* JS_MIR_LIST_FILE = "test/js/mir_list.txt";
static bool js_mixed_mode = false;
static bool js_mir_list_loaded = false;
static bool js_mir_list_available = false;
static char* js_mir_list_contents = NULL;
static size_t js_mir_list_size = 0;

static bool load_js_mir_list() {
    if (js_mir_list_loaded) return js_mir_list_available;
    js_mir_list_loaded = true;

    FILE* file = fopen(JS_MIR_LIST_FILE, "r");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long file_size = ftell(file);
    if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    js_mir_list_contents = (char*)malloc((size_t)file_size + 1);
    if (!js_mir_list_contents) {
        fclose(file);
        return false;
    }
    js_mir_list_size = fread(js_mir_list_contents, 1, (size_t)file_size, file);
    if (js_mir_list_size != (size_t)file_size) {
        free(js_mir_list_contents);
        js_mir_list_contents = NULL;
        js_mir_list_size = 0;
        fclose(file);
        return false;
    }
    js_mir_list_contents[js_mir_list_size] = '\0';
    fclose(file);
    js_mir_list_available = true;
    return js_mir_list_available;
}

static bool js_test_is_in_mir_list(const char* test_name) {
    if (!test_name || !js_mir_list_available || !js_mir_list_contents) return false;
    size_t test_name_size = strlen(test_name);
    const char* line = js_mir_list_contents;
    const char* contents_end = js_mir_list_contents + js_mir_list_size;
    while (line < contents_end) {
        const char* line_end = (const char*)memchr(line, '\n',
            (size_t)(contents_end - line));
        const char* entry_end = line_end ? line_end : contents_end;
        const char* entry = line;
        while (entry < entry_end && isspace((unsigned char)*entry)) entry++;
        const char* comment = (const char*)memchr(entry, '#',
            (size_t)(entry_end - entry));
        if (comment) entry_end = comment;
        while (entry_end > entry && isspace((unsigned char)entry_end[-1])) {
            entry_end--;
        }
        if ((size_t)(entry_end - entry) == test_name_size &&
                strncmp(entry, test_name, test_name_size) == 0) {
            return true;
        }
        if (!line_end) break;
        line = line_end + 1;
    }
    return false;
}

static JsExecutionBackend js_backend_for_test(const JsTestParam& test) {
    if (!js_mixed_mode) return JS_BACKEND_INHERIT;
    return js_test_is_in_mir_list(test.test_name.c_str())
        ? JS_BACKEND_MIR : JS_BACKEND_AST;
}

static bool js_baseline_excludes_library_test(const JsTestParam& test) {
    static const char* excluded_tests[] = {
        "lib_codemirror",
        "lib_tabulator",
        "lib_tom_select",
    };
    for (const char* excluded : excluded_tests) {
        if (strcmp(test.test_name.c_str(), excluded) == 0) return true;
    }
    return false;
}

static void read_js_test_metadata(const char* script_path, JsTestParam* test,
        char* fixture_name, size_t fixture_name_size) {
    if (!script_path || !test || !fixture_name || fixture_name_size == 0) return;
    fixture_name[0] = '\0';
    FILE* file = fopen(script_path, "r");
    if (!file) return;
    char line[512];
    const char* document_prefix = "// @document ";
    const char* module_prefix = "// @test-module-path ";
    for (int line_count = 0; line_count < 8 && fgets(line, sizeof(line), file); line_count++) {
        if (strncmp(line, document_prefix, strlen(document_prefix)) == 0) {
            const char* value = line + strlen(document_prefix);
            size_t value_len = strcspn(value, "\r\n");
            if (value_len > 0 && value_len < fixture_name_size) {
                memcpy(fixture_name, value, value_len);
                fixture_name[value_len] = '\0';
            }
        } else if (strncmp(line, "// @test-permission", 19) == 0) {
            test->permission = true;
        } else if (strncmp(line, module_prefix, strlen(module_prefix)) == 0) {
            const char* value = line + strlen(module_prefix);
            size_t value_len = strcspn(value, "\r\n");
            if (value_len > 0 && value_len < sizeof(test->module_path)) {
                memcpy(test->module_path, value, value_len);
                test->module_path[value_len] = '\0';
            }
        }
    }
    fclose(file);
}

// Discover .js test files in a single directory (one level, no recursion).
// Convention: foo.js + foo.txt = test case.  foo.js + foo.html + foo.txt = DOM test.
static std::vector<JsTestParam> discover_js_tests_in_dir(const char* dir_path) {
    std::vector<JsTestParam> params;

#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.js", dir_path);
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern, &fd);
    if (handle == -1) return params;
    do {
        if (fd.attrib & _A_SUBDIR) continue;
        const char* name = fd.name;
#else
    DIR* dir = opendir(dir_path);
    if (!dir) return params;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) continue;
        const char* name = entry->d_name;
#endif

        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 3, ".js") != 0) continue;

        // base name without .js extension
        std::string base(name, len - 3);
        std::string dir_str(dir_path);

        // must have a matching .txt with expected output
        std::string txt = dir_str + "/" + base + ".txt";
        if (access(txt.c_str(), F_OK) != 0) continue;

        JsTestParam p = {};
        p.script_path   = dir_str + "/" + name;
        p.expected_path  = txt;

        // sanitise test name for GTest (must be valid C identifier)
        p.test_name = base;
        for (auto& c : p.test_name) {
            if (!isalnum((unsigned char)c)) c = '_';
        }

        char fixture_name[256] = {};
        read_js_test_metadata(p.script_path.c_str(), &p,
            fixture_name, sizeof(fixture_name));

        // matching .html → DOM test
        std::string html = dir_str + "/" + base + ".html";
        if (access(html.c_str(), F_OK) == 0) {
            p.html_path = html;
        } else {
            if (fixture_name[0]) {
                char shared_html[1024];
                int shared_len = snprintf(shared_html, sizeof(shared_html), "%s/%s",
                    dir_path, fixture_name);
                // DOM library probes intentionally share one document; without
                // the explicit fixture directive they were batched without a DOM.
                if (shared_len > 0 && shared_len < (int)sizeof(shared_html) &&
                    access(shared_html, F_OK) == 0) {
                    p.html_path = shared_html;
                }
            }
        }

        // These browser-library probes are extended coverage; exclude them only
        // from the fast baseline gate while keeping ordinary JS runs comprehensive.
        if (js_baseline_mode && js_baseline_excludes_library_test(p)) continue;
        params.push_back(p);

#ifdef _WIN32
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    }
    closedir(dir);
#endif

    return params;
}

// Collect JavaScript runtime fixtures; Node/Jube compatibility tests use the
// dedicated test_node_gtest runner and must not enter this suite.
static std::vector<JsTestParam> discover_all_js_tests() {
    static const char* dirs[] = {
        "test/js",
        "test/js/props",  // property-model invariant tests (Stage B harness, see vibe/jube/Transpile_Js38_Refactor.md)
    };
    std::vector<JsTestParam> all;
    for (const char* d : dirs) {
        auto v = discover_js_tests_in_dir(d);
        all.insert(all.end(), v.begin(), v.end());
    }
    // (test/js/editing was retired: those tests asserted native-engine
    // deleteContentBackward DOM parity, which was deleted in Stage 4B Phase 5.
    // Their source-model coverage now lives in the JS editor's own suite —
    // test/editor-js/test/commands/text-commands.test.ts.)
    std::sort(all.begin(), all.end(),
              [](const JsTestParam& a, const JsTestParam& b) {
                  return a.test_name < b.test_name;
              });
    return all;
}

// ---------------------------------------------------------------------------
// Parameterised test — one test case per discovered .js file (batch mode)
// ---------------------------------------------------------------------------

static bool js_gtest_filter_requests_full_batch() {
    std::string filter = ::testing::GTEST_FLAG(filter);
    return filter.empty() || filter == "*" || filter == "JavaScriptTests/JsFileTest.*";
}

class JsFileTest : public testing::TestWithParam<JsTestParam> {
public:
    static std::unordered_map<std::string, JsBatchResult> batch_results;
    static bool batch_executed;

    static void SetUpTestSuite() {
        if (batch_executed) return;
        if (js_mixed_mode && !load_js_mir_list()) {
            fprintf(stderr, "[js-gtest] Cannot load MIR list: %s\n", JS_MIR_LIST_FILE);
            batch_executed = true;
            return;
        }
        if (!js_gtest_filter_requests_full_batch()) {
            // Focused filters must not batch unrelated crash-recovery scripts before the selected case.
            batch_executed = true;
            return;
        }

        // collect non-DOM scripts for batch execution
        auto all = discover_all_js_tests();
        std::vector<std::string> batch_scripts;
        std::vector<std::string> ast_batch_scripts;
        std::vector<std::string> mir_batch_scripts;
        for (const auto& t : all) {
            // Batch workers share one CLI and environment; profile-sensitive
            // scripts must run alone or they silently exercise the wrong host.
            if (t.html_path.empty() && !t.permission && !t.module_path[0]) {
                if (!js_mixed_mode) {
                    batch_scripts.push_back(t.script_path);
                } else if (js_backend_for_test(t) == JS_BACKEND_MIR) {
                    mir_batch_scripts.push_back(t.script_path);
                } else {
                    ast_batch_scripts.push_back(t.script_path);
                }
            }
        }

        size_t chunk_size = js_capture_batch_chunk_size();
        if (!js_mixed_mode) {
            if (!batch_scripts.empty()) {
                batch_results = execute_js_batch(batch_scripts, chunk_size);
            }
        } else {
            std::unordered_map<std::string, JsBatchResult> ast_results;
            std::unordered_map<std::string, JsBatchResult> mir_results;
            std::thread ast_thread;
            std::thread mir_thread;
            if (!ast_batch_scripts.empty()) {
                ast_thread = std::thread([&]() {
                    ast_results = execute_js_batch(ast_batch_scripts, chunk_size,
                        JS_BACKEND_AST);
                });
            }
            if (!mir_batch_scripts.empty()) {
                mir_thread = std::thread([&]() {
                    mir_results = execute_js_batch(mir_batch_scripts, chunk_size,
                        JS_BACKEND_MIR);
                });
            }
            if (ast_thread.joinable()) ast_thread.join();
            if (mir_thread.joinable()) mir_thread.join();
            for (auto& kv : ast_results) batch_results[kv.first] = std::move(kv.second);
            for (auto& kv : mir_results) batch_results[kv.first] = std::move(kv.second);
        }
        batch_executed = true;
    }
};

std::unordered_map<std::string, JsBatchResult> JsFileTest::batch_results;
bool JsFileTest::batch_executed = false;

TEST_P(JsFileTest, Run) {
    const auto& p = GetParam();
    JsExecutionBackend backend = js_backend_for_test(p);

    if (!p.html_path.empty()) {
        // DOM tests: use subprocess fallback (--document flag)
        test_js_dom_script_against_file(
            p.script_path.c_str(), p.html_path.c_str(), p.expected_path.c_str(), backend);
        return;
    }

    // non-DOM tests: use batch result
    char* expected_output = read_expected_output(p.expected_path.c_str());
    ASSERT_NE(expected_output, nullptr) << "Could not read expected: " << p.expected_path;

    // Script may be absent from batch_results if a prior test in the same
    // 50-script worker crashed or hit the per-batch timeout, killing the
    // subprocess before this script's BATCH_END marker was emitted.  Retry
    // in a fresh process — the same retry policy already applied to
    // found-but-failed results below.
    auto it = batch_results.find(p.script_path);
    if (it == batch_results.end()) {
        char* retry_output = execute_js_script_configured(
            p.script_path.c_str(), p.permission, p.module_path, backend);
        ast_tune_append_timing_row("js", p.script_path.c_str(),
            p.test_name.c_str(), g_js_direct_status, &g_js_direct_timing,
            g_js_direct_has_timing, g_js_direct_has_volume);
        ASSERT_NE(retry_output, nullptr)
            << "Script absent from batch results and retry execution failed: "
             << p.script_path;
        strip_js_timing_lines(retry_output);
        trim_trailing_whitespace(retry_output);
        bool match = strcmp(expected_output, retry_output) == 0;
        if (!match) {
            ADD_FAILURE() << "Script absent from batch results and retry output "
                             "did not match expected: " << p.script_path;
        }
        free(retry_output);
        free(expected_output);
        return;
    }

    const JsBatchResult& br = it->second;
    ast_tune_append_timing_row("js", p.script_path.c_str(),
        p.test_name.c_str(), br.status, &br.timing, br.has_timing,
        br.has_volume);

    // extract output (handle ##### Script marker)
    std::string actual = br.output;
    const char* marker = strstr(actual.c_str(), "##### Script");
    if (marker) {
        const char* nl = strchr(marker, '\n');
        if (nl) actual = std::string(nl + 1);
    }

    // trim trailing whitespace
    while (!actual.empty() && isspace((unsigned char)actual.back()))
        actual.pop_back();
    if (!actual.empty()) {
        strip_js_timing_lines(actual.data());
        trim_trailing_whitespace(actual.data());
        actual.assign(actual.data());
    }

    // Batch mode is an optimization.  If a prior test in the same worker
    // crashes or exits through an unusual path, retry this script in a fresh
    // process before reporting a failure.
    if (br.status != 0 || strcmp(expected_output, actual.c_str()) != 0) {
        char* retry_output = execute_js_script_configured(
            p.script_path.c_str(), p.permission, p.module_path, backend);
        if (retry_output) {
            strip_js_timing_lines(retry_output);
            trim_trailing_whitespace(retry_output);
            if (strcmp(expected_output, retry_output) == 0) {
                free(retry_output);
                free(expected_output);
                return;
            }
            actual = retry_output;
            free(retry_output);
        }
    }

    ASSERT_EQ(br.status, 0) << "Script execution failed (exit " << br.status << "): " << p.script_path;
    ASSERT_STREQ(expected_output, actual.c_str())
        << "Output mismatch for: " << p.script_path;

    free(expected_output);
}

INSTANTIATE_TEST_SUITE_P(
    JavaScriptTests,
    JsFileTest,
    testing::ValuesIn(discover_all_js_tests()),
    [](const testing::TestParamInfo<JsTestParam>& info) {
        return info.param.test_name;
    });

// ---------------------------------------------------------------------------
// Standalone: command interface smoke test
// ---------------------------------------------------------------------------

TEST(JavaScriptBasic, CommandInterface) {
    char* output = execute_js_builtin_tests();
    ASSERT_NE(output, nullptr) << "JavaScript command should execute successfully";
    free(output);
}

TEST(JavaScriptRegression, ModuleCompileCacheHonorsPermissionWriteGrants) {
    char output[2048];
#ifdef _WIN32
    const char* denied_command =
        "lambda.exe js --permission -e \"const m=require('node:module');"
        "const r=m.enableCompileCache('./temp/js_cc_perm_denied');"
        "console.log(r.status+':' +(m.getCompileCacheDir()===undefined));\" --no-log 2>&1";
    const char* allowed_command =
        "lambda.exe js --permission -e \"const m=require('node:module');"
        "const r=m.enableCompileCache('./temp/js_cc_perm_allowed');"
        "const d=m.getCompileCacheDir();"
        "const ok=typeof d==='string'?d.indexOf('js_cc_perm_allowed')>=0:false;"
        "console.log(r.status+':' +ok);\" --allow-fs-write ./temp/js_cc_perm_allowed --no-log 2>&1";
#else
    const char* denied_command =
        "./lambda.exe js --permission -e \"const m=require('node:module');"
        "const r=m.enableCompileCache('./temp/js_cc_perm_denied');"
        "console.log(r.status+':' +(m.getCompileCacheDir()===undefined));\" --no-log 2>&1";
    const char* allowed_command =
        "./lambda.exe js --permission -e \"const m=require('node:module');"
        "const r=m.enableCompileCache('./temp/js_cc_perm_allowed');"
        "const d=m.getCompileCacheDir();"
        "const ok=typeof d==='string'?d.indexOf('js_cc_perm_allowed')>=0:false;"
        "console.log(r.status+':' +ok);\" --allow-fs-write ./temp/js_cc_perm_allowed --no-log 2>&1";
#endif

    int status = execute_command_status(denied_command, output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
    ASSERT_NE(strstr(output, "0:true"), nullptr) << output;

    status = execute_command_status(allowed_command, output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
    ASSERT_NE(strstr(output, "1:true"), nullptr) << output;
}

TEST(JavaScriptRegression, ModuleEntryPrelinksOwnAndImportNameTables) {
    const char* source_path = "test/js/module_main.js";
    char manifest_path[256];
    snprintf(manifest_path, sizeof(manifest_path),
        "./temp/js_module_source_%d.txt", (int)getpid());
    ASSERT_TRUE(write_module_source_manifest(source_path, "module_main", manifest_path));

    const char* args[] = {LAMBDA_EXE, "js-test-batch", "--timeout=60", NULL};
    ShellOptions options = {};
    options.stdin_path = manifest_path;
    options.merge_stderr = true;
    options.timeout_ms = 65000;
    ShellResult result = shell_exec(LAMBDA_EXE, args, &options);
    unlink(manifest_path);

    ASSERT_EQ(result.exit_code, 0) << (result.stdout_buf ? result.stdout_buf : "");
    ASSERT_NE(result.stdout_buf, nullptr);
    ASSERT_NE(strstr(result.stdout_buf, "add: 7"), nullptr) << result.stdout_buf;
    ASSERT_NE(strstr(result.stdout_buf, "Hello, World!"), nullptr) << result.stdout_buf;
    ASSERT_NE(strstr(result.stdout_buf, "BATCH_END 0"), nullptr) << result.stdout_buf;
    shell_result_free(&result);
}

TEST(JavaScriptRegression, DocumentExitCodeAfterContextRestoreDoesNotInternWithNullContext) {
    unlink("log.txt");
    char output[4096];
#ifdef _WIN32
    const char* command =
        "lambda.exe js \"test/js/js_document_exit_context.js\" "
        "--document \"test/js/js_document_exit_context.html\" 2>&1";
#else
    const char* command =
        "./lambda.exe js \"test/js/js_document_exit_context.js\" "
        "--document \"test/js/js_document_exit_context.html\" 2>&1";
#endif

    int status = execute_command_status(command, output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
    ASSERT_NE(strstr(output, "A plain-call=3"), nullptr) << output;
    ASSERT_NE(strstr(output, "B dispatched=3"), nullptr) << output;

    char* log_content = read_expected_output("log.txt");
    ASSERT_NE(log_content, nullptr) << "expected logging-enabled JS run to create log.txt";
    ASSERT_EQ(strstr(log_content, "heap_create_name called with invalid context or name_pool"), nullptr)
        << log_content;
    ASSERT_EQ(strstr(log_content, "map_get: key must be string or symbol, got type null"), nullptr)
        << log_content;
    free(log_content);
}

TEST(JavaScriptFuzz, FuzzIifeObjectLabelBlockDoesNotTimeout) {
    char output[2048];
    int status = execute_js_script_status("test/js/fuzz_iife_invalid_object_block.js",
                                          output, sizeof(output));
#ifndef _WIN32
    ASSERT_NE(status, 124) << "IIFE object-like label block should not timeout";
#endif
    ASSERT_EQ(status, 0) << output;
}

TEST(JavaScriptFuzz, FuzzGeneratorObjectLabelBlockDoesNotTimeout) {
    char output[2048];
    int status = execute_js_script_status("test/js/fuzz_generator_invalid_object_block.js",
                                          output, sizeof(output));
#ifndef _WIN32
    ASSERT_NE(status, 124) << "Generator object-like label block should not timeout";
#endif
    ASSERT_EQ(status, 0) << output;
}

TEST(JavaScriptFuzz, FuzzScopeClosureShadowDoesNotCrash) {
    char output[2048];
    int status = execute_js_script_status("test/js/fuzz_scope_closure_shadow_crash.js",
                                          output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
}

// Js54 P0: indexed write on a typed array after a user property write used to
// SIGSEGV because the MIR JIT loaded Map.data at offset 16 as a JsTypedArray*
// without checking data_cap — after the upgrade triggered by the property
// write, that load returns the property-storage buffer and the next sized
// store writes into wild memory. Closes the crash test
// built-ins/TypedArray/out-of-bounds-behaves-like-detached.js.
TEST(JavaScriptRegression, Js54P0TypedArrayProtoSwapDoesNotSigsegv) {
    char output[2048];
    int status = execute_js_script_status(
        "test/js/regression_js54_p0_typed_array_proto_swap_sigsegv.js",
        output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
}

// Js54 P2: DataView OOB-aware accessors. Fixed-length views throw on
// shrink-past-end; length-tracking views update their byteLength live with the
// buffer; OOB and detached buffers throw TypeError on every accessor.
TEST(JavaScriptRegression, Js54P2DataViewOobAccessors) {
    char output[2048];
    int status = execute_js_script_status(
        "test/js/regression_js54_p2_dataview_oob.js",
        output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
}

// Js54 P3: TypedArray length-tracking + OOB indexed access. Length-tracking
// views update their length live on resize; OOB and detached buffers report
// length 0, return undefined on indexed read, and silently no-op on write.
// Critically, the JIT must re-read the data pointer each access — the buffer
// resize reallocs ab->data and any cached snapshot would be stale.
TEST(JavaScriptRegression, Js54P3TypedArrayLengthTracking) {
    char output[2048];
    int status = execute_js_script_status(
        "test/js/regression_js54_p3_typed_array_length_tracking.js",
        output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
}

// ArrayNum issue 38: calls inside a subscript loop may resize, transfer, or
// detach a typed array's backing buffer. P4h must therefore use the live
// per-access pointer and length instead of snapshots taken before the loop.
TEST(JavaScriptRegression, ArrayNumLoopResizeInvalidatesHoist) {
    char output[2048];
    int status = execute_js_script_status(
        "test/js/regression_js_arraynum_loop_resize_hoist.js",
        output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
}

// Js54 P4: TypedArray prototype methods over resizable buffers. The shared
// shape: each method calls ValidateTypedArray at entry (throw TypeError on
// detached or OOB). Several methods (slice, forEach, reduce, reduceRight,
// join, toLocaleString, sort, with, toReversed, toSorted) were missing this
// check. indexOf/lastIndexOf capture len BEFORE coercion callbacks and skip
// post-coercion-detached/OOB positions per spec HasProperty semantics. The
// raw_index_of fast path returns -2 for non-numeric search values so the
// slow path (which uses Get) can match undefined at OOB positions for
// includes(undefined, ...).
TEST(JavaScriptRegression, Js54P4TypedArrayPrototypeOob) {
    char output[2048];
    int status = execute_js_script_status(
        "test/js/regression_js54_p4_typed_array_proto_oob.js",
        output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
}

// Js54 P5: Array.prototype methods called on TypedArray receivers. Methods
// like every/forEach/slice/indexOf/... share JS_BUILTIN_ARR_* between Array
// and TypedArray, but spec-diverge on OOB: TypedArray.prototype.X throws via
// ValidateTypedArray, Array.prototype.X uses LengthOfArrayLike (0 for OOB) and
// silently no-ops. js_call_function and js_invoke_fn now flip
// js_dispatch_as_array_method based on the calling fn's TYPED_ARRAY_METHOD
// flag; the per-method object-intrinsic OOB checks gate on it.
TEST(JavaScriptRegression, Js54P5ArrayProtoOnTypedArray) {
    char output[2048];
    int status = execute_js_script_status(
        "test/js/regression_js54_p5_array_proto_on_ta.js",
        output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
}

// Js54 P6: Extend the P5 dispatch-mode gating into the runtime helpers
// js_typed_array_fill / js_typed_array_set_from / js_typed_array_slice, which
// had their own ValidateTypedArray-style OOB throws that fired regardless of
// dispatch mode. Array.prototype.{fill,set,slice}.call(ta_oob, ...) now
// silently no-op as the spec's LengthOfArrayLike path prescribes.
TEST(JavaScriptRegression, Js54P6ArrayProtoFillSetSlice) {
    char output[2048];
    int status = execute_js_script_status(
        "test/js/regression_js54_p6_array_proto_fill_set_slice.js",
        output, sizeof(output));
    ASSERT_EQ(status, 0) << output;
}

static void parse_js_gtest_options(int* argc, char** argv) {
    if (!argc || !argv) return;
    const char* mode = getenv("JS_GTEST_MODE");
    js_mixed_mode = !mode || strcmp(mode, "mir") != 0;

    int write_index = 1;
    for (int read_index = 1; read_index < *argc; read_index++) {
        if (strcmp(argv[read_index], "--mixed-mode") == 0) {
            js_mixed_mode = true;
            continue;
        }
        if (strcmp(argv[read_index], "--full-mir") == 0) {
            js_mixed_mode = false;
            continue;
        }
        argv[write_index++] = argv[read_index];
    }
    *argc = write_index;
    argv[write_index] = NULL;
    if (js_mixed_mode) {
        // make every unlisted direct or shell-launched JS check AST by default;
        // listed parameterized cases override this in their child environment.
        shell_setenv("JS_EXECUTION_BACKEND", "ast");
    }
}

int main(int argc, char **argv) {
    parse_js_gtest_options(&argc, argv);
    js_baseline_mode = test_parse_baseline_mode(&argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
