//==============================================================================
// Lambda Structured Tests (test/std/) - Auto-Discovery Based
//
// This file auto-discovers and tests Lambda scripts in test/std/ against
// their .expected output files. It recursively scans all subdirectories
// under test/std/ for .ls files with matching .expected files.
//==============================================================================

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #define NOHELP
    #define NOMCX
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
    #define LAMBDA_EXE "lambda.exe"
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

//==============================================================================
// Test Info Structure
//==============================================================================

struct StdTestInfo {
    std::string script_path;    // e.g. "test/std/core/datatypes/integer_basic.ls"
    std::string expected_path;  // e.g. "test/std/core/datatypes/integer_basic.expected"
    std::string test_name;      // e.g. "core_datatypes_integer_basic"

    friend std::ostream& operator<<(std::ostream& os, const StdTestInfo& info) {
        return os << info.test_name;
    }
};

//==============================================================================
// Helper Functions
//==============================================================================

static bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (f) { fclose(f); return true; }
    return false;
}

// Returns platform-specific expected path (.win.expected / .linux.expected / .mac.expected)
// if one exists, otherwise the generic .expected path.
static std::string platform_expected(const std::string& base_expected) {
    // base_expected ends with ".expected"
    std::string stem = base_expected.substr(0, base_expected.length() - 9); // strip ".expected"
#if defined(_WIN32)
    std::string plat = stem + ".win.expected";
#elif defined(__linux__)
    std::string plat = stem + ".linux.expected";
#elif defined(__APPLE__)
    std::string plat = stem + ".mac.expected";
#else
    std::string plat = "";
#endif
    if (!plat.empty() && file_exists(plat)) return plat;
    return base_expected;
}

static void trim_trailing_whitespace(char* str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Build test name from path relative to test/std/
// e.g. "test/std/core/datatypes/integer_basic.ls" -> "core_datatypes_integer_basic"
static std::string make_test_name(const std::string& script_path) {
    // Strip "test/std/" prefix
    const std::string prefix = "test/std/";
    std::string relative = script_path;
    if (relative.find(prefix) == 0) {
        relative = relative.substr(prefix.size());
    }

    // Strip ".ls" extension
    size_t dot_pos = relative.find_last_of('.');
    if (dot_pos != std::string::npos) {
        relative = relative.substr(0, dot_pos);
    }

    // Replace / with _
    for (char& c : relative) {
        if (c == '/' || c == '\\') c = '_';
        else if (!isalnum(c) && c != '_') c = '_';
    }

    return relative;
}

// Check if a script file contains '// Mode: procedural' annotation
static bool is_procedural_mode(const char* script_path) {
    FILE* f = fopen(script_path, "r");
    if (!f) return false;
    char line[256];
    // check first 5 lines for mode annotation
    for (int i = 0; i < 5 && fgets(line, sizeof(line), f); i++) {
        if (strstr(line, "// Mode: procedural")) {
            fclose(f);
            return true;
        }
    }
    fclose(f);
    return false;
}

// Execute a lambda script, capture stdout only (stderr discarded)
// Uses --c2mir to ensure C2MIR path (these structured tests target C2MIR features)
static char* execute_script(const char* script_path) {
    char command[512];
    bool procedural = is_procedural_mode(script_path);
#ifdef _WIN32
    if (procedural)
        snprintf(command, sizeof(command), "lambda.exe run --no-log --c2mir \"%s\" 2>NUL", script_path);
    else
        snprintf(command, sizeof(command), "lambda.exe --no-log --c2mir \"%s\" 2>NUL", script_path);
#else
    if (procedural)
        snprintf(command, sizeof(command), "./lambda.exe run --no-log --c2mir \"%s\" 2>/dev/null", script_path);
    else
        snprintf(command, sizeof(command), "./lambda.exe --no-log --c2mir \"%s\" 2>/dev/null", script_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error: Could not execute: %s\n", command);
        return nullptr;
    }

    char buffer[4096];
    size_t total_size = 0;
    char* output = nullptr;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        char* new_output = (char*)realloc(output, total_size + len + 1);
        if (!new_output) {
            free(output);
            pclose(pipe);
            return nullptr;
        }
        output = new_output;
        memcpy(output + total_size, buffer, len);
        total_size += len;
        output[total_size] = '\0';
    }

    pclose(pipe);

    if (!output) {
        return strdup("");
    }

    // If output contains "##### Script" marker, extract everything after it
    char* marker = strstr(output, "##### Script");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++; // skip newline
            char* result = strdup(result_start);
            free(output);
            return result;
        }
    }

    return output;
}

// Read file contents
static char* read_file_contents(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) return nullptr;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    if (!content) { fclose(file); return nullptr; }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    return content;
}

//==============================================================================
// Recursive Directory Scanning
//==============================================================================

static void discover_tests_recursive(const char* dir_path, std::vector<StdTestInfo>& tests) {
#ifdef _WIN32
    // Windows: use FindFirstFile/FindNextFile
    std::string search = std::string(dir_path) + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;

        std::string full_path = std::string(dir_path) + "/" + name;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            discover_tests_recursive(full_path.c_str(), tests);
        } else if (name.size() > 3 && name.substr(name.size() - 3) == ".ls") {
            std::string expected = full_path.substr(0, full_path.size() - 3) + ".expected";
            expected = platform_expected(expected);
            if (file_exists(expected)) {
                StdTestInfo info;
                info.script_path = full_path;
                info.expected_path = expected;
                info.test_name = make_test_name(full_path);
                tests.push_back(info);
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    // POSIX: use opendir/readdir
    DIR* dir = opendir(dir_path);
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        std::string full_path = std::string(dir_path) + "/" + name;

        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            discover_tests_recursive(full_path.c_str(), tests);
        } else if (name.size() > 3 && name.substr(name.size() - 3) == ".ls") {
            std::string expected = full_path.substr(0, full_path.size() - 3) + ".expected";
            expected = platform_expected(expected);
            if (file_exists(expected)) {
                StdTestInfo info;
                info.script_path = full_path;
                info.expected_path = expected;
                info.test_name = make_test_name(full_path);
                tests.push_back(info);
            }
        }
    }
    closedir(dir);
#endif
}

static std::vector<StdTestInfo> discover_std_tests() {
    std::vector<StdTestInfo> tests;
    discover_tests_recursive("test/std", tests);
    std::sort(tests.begin(), tests.end(), [](const StdTestInfo& a, const StdTestInfo& b) {
        return a.test_name < b.test_name;
    });
    return tests;
}

#include <unordered_map>

//==============================================================================
// Batch Execution Support for Std Tests
//==============================================================================

struct StdBatchResult {
    std::string output;
    int status;
};

static const size_t STD_BATCH_CHUNK_SIZE = 50;

static void run_std_sub_batch(
    const std::vector<StdTestInfo>& tests,
    size_t start, size_t end,
    int batch_id,
    std::unordered_map<std::string, StdBatchResult>& results)
{
    char manifest_path[128];
    snprintf(manifest_path, sizeof(manifest_path), "./temp/batch_std_%d_%d.txt", (int)getpid(), batch_id);
    FILE* manifest = fopen(manifest_path, "w");
    if (!manifest) return;

    for (size_t i = start; i < end; i++) {
        bool procedural = is_procedural_mode(tests[i].script_path.c_str());
        if (procedural) {
            fprintf(manifest, "run %s\n", tests[i].script_path.c_str());
        } else {
            fprintf(manifest, "%s\n", tests[i].script_path.c_str());
        }
    }
    fclose(manifest);

    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command),
             "lambda.exe test-batch --no-log --c2mir < \"%s\"", manifest_path);
#else
    snprintf(command, sizeof(command),
             "./lambda.exe test-batch --no-log --c2mir < \"%s\"", manifest_path);
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
}

static std::unordered_map<std::string, StdBatchResult> execute_std_batch(
    const std::vector<StdTestInfo>& tests)
{
    std::unordered_map<std::string, StdBatchResult> results;
    if (tests.empty()) return results;

    int batch_id = 0;
    for (size_t start = 0; start < tests.size(); start += STD_BATCH_CHUNK_SIZE) {
        size_t end = std::min(start + STD_BATCH_CHUNK_SIZE, tests.size());
        run_std_sub_batch(tests, start, end, batch_id++, results);
    }
    return results;
}

//==============================================================================
// Parameterized Test (Batch Mode)
//==============================================================================

static std::vector<StdTestInfo> g_std_tests;

class LambdaStdTest : public ::testing::TestWithParam<StdTestInfo> {
public:
    static std::unordered_map<std::string, StdBatchResult> batch_results;
    static bool batch_executed;

    static void SetUpTestSuite() {
        if (batch_executed) return;

        // Determine shard subset
        const char* total_env = getenv("GTEST_TOTAL_SHARDS");
        const char* index_env = getenv("GTEST_SHARD_INDEX");
        int total_shards = total_env ? atoi(total_env) : 1;
        int shard_index = index_env ? atoi(index_env) : 0;

        std::vector<StdTestInfo> shard_tests;
        for (size_t i = 0; i < g_std_tests.size(); i++) {
            if ((int)(i % total_shards) == shard_index) {
                shard_tests.push_back(g_std_tests[i]);
            }
        }

        batch_results = execute_std_batch(shard_tests);
        batch_executed = true;
    }
};

std::unordered_map<std::string, StdBatchResult> LambdaStdTest::batch_results;
bool LambdaStdTest::batch_executed = false;

TEST_P(LambdaStdTest, ExecuteAndCompare) {
    const StdTestInfo& info = GetParam();

    auto it = batch_results.find(info.script_path);
    ASSERT_TRUE(it != batch_results.end())
        << "Script not found in batch results: " << info.script_path;

    const StdBatchResult& br = it->second;
    ASSERT_EQ(br.status, 0) << "Script execution failed: " << info.script_path;

    // Extract output after marker (if present)
    char* raw = strdup(br.output.c_str());
    char* actual = raw;
    char* marker = strstr(raw, "##### Script");
    if (marker) {
        char* start = strchr(marker, '\n');
        if (start) {
            start++;
            actual = strdup(start);
            free(raw);
            raw = actual;
        }
    }
    trim_trailing_whitespace(actual);

    char* expected = read_file_contents(info.expected_path.c_str());
    ASSERT_NE(expected, nullptr) << "Could not read expected file: " << info.expected_path;
    trim_trailing_whitespace(expected);

    ASSERT_STREQ(expected, actual)
        << "Output mismatch for: " << info.script_path;

    free(expected);
    free(raw);
}

std::string StdTestNameGenerator(const ::testing::TestParamInfo<StdTestInfo>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    Std,
    LambdaStdTest,
    ::testing::ValuesIn(g_std_tests),
    StdTestNameGenerator
);

//==============================================================================
// Main
//==============================================================================

int main(int argc, char** argv) {
    g_std_tests = discover_std_tests();

    printf("Discovered %zu structured tests (test/std/):\n", g_std_tests.size());
    for (const auto& t : g_std_tests) {
        printf("  - %s\n", t.test_name.c_str());
    }
    printf("\n");

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
