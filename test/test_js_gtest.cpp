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
    #ifndef F_OK
    #define F_OK 0
    #endif
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <dirent.h>
#endif

// Helper function to execute a JavaScript file with lambda js and capture output
char* execute_js_script(const char* script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js \"%s\" --no-log", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe js \"%s\" --no-log", script_path);
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
        fprintf(stderr, "Error: lambda.exe js exited with code %d for script: %s\n",
                WEXITSTATUS(exit_code), script_path);
        free(full_output);
        return nullptr;
    }

    // Return empty string for successful but empty output
    if (!full_output) {
        return strdup("");
    }
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

int execute_js_script_status(const char* script_path, char* output, size_t output_size) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js \"%s\" --no-log 2>&1", script_path);
#else
    snprintf(command, sizeof(command), "timeout 5 ./lambda.exe js \"%s\" --no-log 2>&1", script_path);
#endif

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

// Helper function to test JavaScript script against expected output file
void test_js_script_against_file(const char* script_path, const char* expected_file_path) {
    // Get script name for better error messages
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_js_script(script_path);
    ASSERT_NE(actual_output, nullptr) << "Could not execute JavaScript script: " << script_path;

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
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js --no-log 2>&1");
#else
    snprintf(command, sizeof(command), "./lambda.exe js --no-log 2>&1");
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
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
    
    // Even if there's no output, if the command succeeded (exit code 0), return an empty string
    if (WEXITSTATUS(exit_code) != 0) {
        free(full_output);
        return nullptr;
    }
    
    // If no output was produced but command succeeded, return empty string
    if (full_output == nullptr) {
        full_output = (char*)calloc(1, 1);  // Empty string
    }

    return full_output;
}

// Helper function to execute a JavaScript file with --document flag and capture output
char* execute_js_script_with_doc(const char* script_path, const char* html_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js \"%s\" --document \"%s\" --no-log", script_path, html_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe js \"%s\" --document \"%s\" --no-log", script_path, html_path);
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
        fprintf(stderr, "Error: lambda.exe js exited with code %d for script: %s --document %s\n",
                WEXITSTATUS(exit_code), script_path, html_path);
        free(full_output);
        return nullptr;
    }

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
void test_js_dom_script_against_file(const char* script_path, const char* html_path, const char* expected_file_path) {
    const char* script_name = strrchr(script_path, '/');
    script_name = script_name ? script_name + 1 : script_path;

    char* expected_output = read_expected_output(expected_file_path);
    ASSERT_NE(expected_output, nullptr) << "Could not read expected output file: " << expected_file_path;

    char* actual_output = execute_js_script_with_doc(script_path, html_path);
    ASSERT_NE(actual_output, nullptr) << "Could not execute JavaScript DOM script: " << script_path;

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
};

// Max scripts per lambda.exe js-test-batch process
static const size_t JS_BATCH_CHUNK_SIZE = 50;

static void run_js_sub_batch(
    const std::vector<std::string>& scripts,
    size_t start, size_t end,
    int batch_id,
    std::unordered_map<std::string, JsBatchResult>& results)
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

    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js-test-batch --timeout=60 < \"%s\"", manifest_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe js-test-batch --timeout=60 < \"%s\"", manifest_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        unlink(manifest_path);
        return;
    }

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

static std::unordered_map<std::string, JsBatchResult> execute_js_batch(
    const std::vector<std::string>& scripts,
    size_t chunk_size = JS_BATCH_CHUNK_SIZE)
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
                             batches[i].id, thread_results[i]);
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
};

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

        JsTestParam p;
        p.script_path   = dir_str + "/" + name;
        p.expected_path  = txt;

        // sanitise test name for GTest (must be valid C identifier)
        p.test_name = base;
        for (auto& c : p.test_name) {
            if (!isalnum((unsigned char)c)) c = '_';
        }

        // matching .html → DOM test
        std::string html = dir_str + "/" + base + ".html";
        if (access(html.c_str(), F_OK) == 0) {
            p.html_path = html;
        }

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

// Collect tests from all configured directories.
// Add new directories here as needed.
static std::vector<JsTestParam> discover_all_js_tests() {
    static const char* dirs[] = {
        "test/js",
        "test/node",
        "test/js/props",  // property-model invariant tests (Stage B harness, see vibe/jube/Transpile_Js38_Refactor.md)
    };
    std::vector<JsTestParam> all;
    for (const char* d : dirs) {
        auto v = discover_js_tests_in_dir(d);
        all.insert(all.end(), v.begin(), v.end());
    }
    const char* include_editing = getenv("LAMBDA_JS_INCLUDE_EDITING_TESTS");
    if (include_editing && strcmp(include_editing, "1") == 0) {
        auto v = discover_js_tests_in_dir("test/js/editing");
        all.insert(all.end(), v.begin(), v.end());
    }
    std::sort(all.begin(), all.end(),
              [](const JsTestParam& a, const JsTestParam& b) {
                  return a.test_name < b.test_name;
              });
    return all;
}

// ---------------------------------------------------------------------------
// Parameterised test — one test case per discovered .js file (batch mode)
// ---------------------------------------------------------------------------

class JsFileTest : public testing::TestWithParam<JsTestParam> {
public:
    static std::unordered_map<std::string, JsBatchResult> batch_results;
    static bool batch_executed;

    static void SetUpTestSuite() {
        if (batch_executed) return;

        // collect non-DOM scripts for batch execution
        auto all = discover_all_js_tests();
        std::vector<std::string> batch_scripts;
        for (const auto& t : all) {
            if (t.html_path.empty()) {
                batch_scripts.push_back(t.script_path);
            }
        }

        if (!batch_scripts.empty()) {
            batch_results = execute_js_batch(batch_scripts);
        }
        batch_executed = true;
    }
};

std::unordered_map<std::string, JsBatchResult> JsFileTest::batch_results;
bool JsFileTest::batch_executed = false;

TEST_P(JsFileTest, Run) {
    const auto& p = GetParam();

    if (!p.html_path.empty()) {
        // DOM tests: use subprocess fallback (--document flag)
        test_js_dom_script_against_file(
            p.script_path.c_str(), p.html_path.c_str(), p.expected_path.c_str());
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
        char* retry_output = execute_js_script(p.script_path.c_str());
        ASSERT_NE(retry_output, nullptr)
            << "Script absent from batch results and retry execution failed: "
            << p.script_path;
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

    // Batch mode is an optimization.  If a prior test in the same worker
    // crashes or exits through an unusual path, retry this script in a fresh
    // process before reporting a failure.
    if (br.status != 0 || strcmp(expected_output, actual.c_str()) != 0) {
        char* retry_output = execute_js_script(p.script_path.c_str());
        if (retry_output) {
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
// flag; the per-method OOB-throw blocks in js_map_method gate on it.
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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
