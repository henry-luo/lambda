//==============================================================================
// TypeScript Transpiler Tests - Auto-Discovery
//
// Discovers test/ts/*.ts scripts, executes them via ./lambda.exe ts,
// and compares output against matching .txt expected-result files.
//==============================================================================

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

extern "C" {
#include "../lib/shell.h"
}

#ifdef _WIN32
    #include <process.h>
    #include <io.h>
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
    #define LAMBDA_EXE "lambda.exe"
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

// ============================================================================
// Test info
// ============================================================================

struct TsTestInfo {
    std::string script_path;
    std::string expected_path;
    std::string test_name;

    friend std::ostream& operator<<(std::ostream& os, const TsTestInfo& info) {
        return os << info.test_name;
    }
};

// ============================================================================
// Helpers
// ============================================================================

static bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static std::string make_test_name(const std::string& filename) {
    // "arrow_functions.ts" -> "arrow_functions"
    std::string name = filename;
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    // sanitise for GTest (only alnum and _)
    for (auto& c : name) {
        if (!isalnum(c) && c != '_') c = '_';
    }
    return name;
}

static std::vector<TsTestInfo> discover_ts_tests(const char* dir_path) {
    std::vector<TsTestInfo> tests;
    DIR* dir = opendir(dir_path);
    if (!dir) return tests;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fname = entry->d_name;
        if (fname.length() > 3 && fname.substr(fname.length() - 3) == ".ts") {
            std::string script = std::string(dir_path) + "/" + fname;
            std::string expected = script.substr(0, script.length() - 3) + ".txt";
            if (file_exists(expected)) {
                TsTestInfo info;
                info.script_path = script;
                info.expected_path = expected;
                info.test_name = make_test_name(fname);
                tests.push_back(info);
            }
        }
    }
    closedir(dir);

    std::sort(tests.begin(), tests.end(),
        [](const TsTestInfo& a, const TsTestInfo& b) { return a.test_name < b.test_name; });
    return tests;
}

// Execute a TS script via CLI, return captured stdout
static char* execute_ts_script(const char* script_path) {
    const char* args[] = {LAMBDA_EXE, "ts", script_path, "--no-log", NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    // Script paths must remain single argv values even when they contain shell characters.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (shell_result.exit_code != 0) {
        fprintf(stderr, "Error: lambda.exe ts exited with code %d for: %s\n",
                shell_result.exit_code, script_path);
        shell_result_free(&shell_result);
        return nullptr;
    }
    char* output = shell_result.stdout_buf ? strdup(shell_result.stdout_buf) : strdup("");
    shell_result_free(&shell_result);
    return output;
}

static char* read_expected_output(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return nullptr;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return nullptr; }
    size_t n = fread(buf, 1, sz, f);
    buf[n] = '\0';
    fclose(f);

    // trim trailing whitespace
    while (n > 0 && isspace((unsigned char)buf[n - 1])) buf[--n] = '\0';
    return buf;
}

static void trim_trailing_whitespace(char* s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

// ============================================================================
// Parameterized test fixture
// ============================================================================

static std::vector<TsTestInfo> g_ts_tests;

class TypeScriptTest : public ::testing::TestWithParam<TsTestInfo> {};

TEST_P(TypeScriptTest, ExecuteAndCompare) {
    const TsTestInfo& info = GetParam();

    char* actual = execute_ts_script(info.script_path.c_str());
    ASSERT_NE(actual, nullptr)
        << "Could not execute TypeScript script: " << info.script_path;
    trim_trailing_whitespace(actual);

    char* expected = read_expected_output(info.expected_path.c_str());
    ASSERT_NE(expected, nullptr)
        << "Could not read expected file: " << info.expected_path;

    EXPECT_STREQ(expected, actual)
        << "Output mismatch for: " << info.script_path
        << "\nExpected:\n" << expected
        << "\nActual:\n" << actual;

    free(expected);
    free(actual);
}

std::string TsTestNameGenerator(
    const ::testing::TestParamInfo<TsTestInfo>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    TypeScript,
    TypeScriptTest,
    ::testing::ValuesIn(g_ts_tests),
    TsTestNameGenerator
);

// ============================================================================
// main — discover tests, then hand off to GTest
// ============================================================================

int main(int argc, char** argv) {
    g_ts_tests = discover_ts_tests("test/ts");

    printf("Discovered %zu TypeScript tests:\n", g_ts_tests.size());
    for (const auto& t : g_ts_tests)
        printf("  - %s\n", t.test_name.c_str());
    printf("\n");

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
