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

// Execute a lambda script, capture stdout only (stderr discarded)
static char* execute_script(const char* script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe \"%s\" 2>NUL", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe \"%s\" 2>/dev/null", script_path);
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

//==============================================================================
// Parameterized Test
//==============================================================================

static std::vector<StdTestInfo> g_std_tests;

class LambdaStdTest : public ::testing::TestWithParam<StdTestInfo> {};

TEST_P(LambdaStdTest, ExecuteAndCompare) {
    const StdTestInfo& info = GetParam();

    // Read expected output
    char* expected = read_file_contents(info.expected_path.c_str());
    ASSERT_NE(expected, nullptr)
        << "Could not read expected file: " << info.expected_path;
    trim_trailing_whitespace(expected);

    // Execute script
    char* actual = execute_script(info.script_path.c_str());
    ASSERT_NE(actual, nullptr)
        << "Could not execute script: " << info.script_path;
    trim_trailing_whitespace(actual);

    // Compare
    ASSERT_STREQ(expected, actual)
        << "Output mismatch for: " << info.script_path;

    free(expected);
    free(actual);
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
