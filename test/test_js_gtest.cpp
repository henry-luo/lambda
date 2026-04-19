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
    snprintf(command, sizeof(command), "lambda.exe js-test-batch --timeout=60 --opt-level=0 < \"%s\"", manifest_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe js-test-batch --timeout=60 --opt-level=0 < \"%s\"", manifest_path);
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
    };
    std::vector<JsTestParam> all;
    for (const char* d : dirs) {
        auto v = discover_js_tests_in_dir(d);
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
    auto it = batch_results.find(p.script_path);
    ASSERT_TRUE(it != batch_results.end())
        << "Script not found in batch results: " << p.script_path;

    const JsBatchResult& br = it->second;
    ASSERT_EQ(br.status, 0) << "Script execution failed (exit " << br.status << "): " << p.script_path;

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

    char* expected_output = read_expected_output(p.expected_path.c_str());
    ASSERT_NE(expected_output, nullptr) << "Could not read expected: " << p.expected_path;

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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
