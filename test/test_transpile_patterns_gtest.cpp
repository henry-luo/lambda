//==============================================================================
// Transpile Pattern Tests - Verify Generated C Code Patterns
//
// This file auto-discovers .transpile fixture files alongside .ls test scripts
// and verifies that the transpiled C code contains expected patterns and
// does not contain forbidden patterns.
//
// Fixture format: For script abc.ls, create abc.transpile as JSON:
// {
//   "expect": ["fn_pow_u", "push_d(fn_pow_u", ...],
//   "forbid": ["fn_pow(", ...]
// }
//==============================================================================

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <regex>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define getcwd _getcwd
    #define popen _popen
    #define pclose _pclose
    #define unlink _unlink
    #define LAMBDA_EXE "lambda.exe"
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define LAMBDA_EXE "./lambda.exe"
#endif

//==============================================================================
// Transpile Test Info
//==============================================================================

struct TranspileTestInfo {
    std::string script_path;       // Path to .ls file
    std::string fixture_path;      // Path to .transpile file
    std::string test_name;         // Test name for display

    friend std::ostream& operator<<(std::ostream& os, const TranspileTestInfo& info) {
        return os << info.test_name;
    }
};

//==============================================================================
// Simple JSON Parser for Transpile Fixtures
//==============================================================================

// Minimal JSON parser for the transpile fixture format
// Parses: { "expect": [...], "forbid": [...] }
struct TranspileFixture {
    std::vector<std::string> expect;
    std::vector<std::string> forbid;

    static TranspileFixture parse(const std::string& json_content) {
        TranspileFixture fixture;

        // Find "expect" array
        size_t expect_pos = json_content.find("\"expect\"");
        if (expect_pos != std::string::npos) {
            fixture.expect = parse_string_array(json_content, expect_pos);
        }

        // Find "forbid" array
        size_t forbid_pos = json_content.find("\"forbid\"");
        if (forbid_pos != std::string::npos) {
            fixture.forbid = parse_string_array(json_content, forbid_pos);
        }

        return fixture;
    }

private:
    static std::vector<std::string> parse_string_array(const std::string& json, size_t start_pos) {
        std::vector<std::string> result;

        // Find the opening bracket
        size_t bracket_start = json.find('[', start_pos);
        if (bracket_start == std::string::npos) return result;

        size_t bracket_end = json.find(']', bracket_start);
        if (bracket_end == std::string::npos) return result;

        std::string array_content = json.substr(bracket_start + 1, bracket_end - bracket_start - 1);

        // Parse each string in the array
        size_t pos = 0;
        while (pos < array_content.length()) {
            // Find opening quote
            size_t quote_start = array_content.find('"', pos);
            if (quote_start == std::string::npos) break;

            // Find closing quote (handling escaped quotes)
            size_t quote_end = quote_start + 1;
            while (quote_end < array_content.length()) {
                if (array_content[quote_end] == '"' && array_content[quote_end - 1] != '\\') {
                    break;
                }
                quote_end++;
            }

            if (quote_end < array_content.length()) {
                std::string value = array_content.substr(quote_start + 1, quote_end - quote_start - 1);
                // Unescape basic sequences
                size_t escape_pos;
                while ((escape_pos = value.find("\\\"")) != std::string::npos) {
                    value.replace(escape_pos, 2, "\"");
                }
                while ((escape_pos = value.find("\\n")) != std::string::npos) {
                    value.replace(escape_pos, 2, "\n");
                }
                while ((escape_pos = value.find("\\\\")) != std::string::npos) {
                    value.replace(escape_pos, 2, "\\");
                }
                result.push_back(value);
            }

            pos = quote_end + 1;
        }

        return result;
    }
};

//==============================================================================
// Test Discovery
//==============================================================================

// Discover all .transpile fixture files in a directory
std::vector<TranspileTestInfo> discover_transpile_tests(const char* directory) {
    std::vector<TranspileTestInfo> tests;

#ifdef _WIN32
    std::string pattern = std::string(directory) + "\\*.transpile";
    WIN32_FIND_DATAA find_data;
    HANDLE handle = FindFirstFileA(pattern.c_str(), &find_data);

    if (handle != INVALID_HANDLE_VALUE) {
        do {
            std::string fixture_name = find_data.cFileName;
            std::string base_name = fixture_name.substr(0, fixture_name.length() - 10); // Remove ".transpile"

            TranspileTestInfo info;
            info.fixture_path = std::string(directory) + "/" + fixture_name;
            info.script_path = std::string(directory) + "/" + base_name + ".ls";
            info.test_name = base_name;

            // Replace invalid characters for test name
            std::replace(info.test_name.begin(), info.test_name.end(), '-', '_');
            std::replace(info.test_name.begin(), info.test_name.end(), '.', '_');

            tests.push_back(info);
        } while (FindNextFileA(handle, &find_data));
        FindClose(handle);
    }
#else
    DIR* dir = opendir(directory);
    if (!dir) return tests;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Check if file ends with .transpile
        if (filename.length() > 10 &&
            filename.substr(filename.length() - 10) == ".transpile") {

            std::string base_name = filename.substr(0, filename.length() - 10);

            TranspileTestInfo info;
            info.fixture_path = std::string(directory) + "/" + filename;
            info.script_path = std::string(directory) + "/" + base_name + ".ls";
            info.test_name = base_name;

            // Replace invalid characters for test name
            std::replace(info.test_name.begin(), info.test_name.end(), '-', '_');
            std::replace(info.test_name.begin(), info.test_name.end(), '.', '_');

            // Check that the .ls file exists
            struct stat st;
            if (stat(info.script_path.c_str(), &st) == 0) {
                tests.push_back(info);
            }
        }
    }
    closedir(dir);
#endif

    // Sort by test name for consistent ordering
    std::sort(tests.begin(), tests.end(),
              [](const TranspileTestInfo& a, const TranspileTestInfo& b) {
                  return a.test_name < b.test_name;
              });

    return tests;
}

//==============================================================================
// Transpile and Capture Generated Code
//==============================================================================

// Execute lambda.exe to transpile a script and return the generated C code
std::string transpile_and_get_code(const std::string& script_path) {
    // Build command to execute lambda.exe
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe \"%s\" 2>&1", script_path.c_str());
#else
    snprintf(command, sizeof(command), "./lambda.exe \"%s\" 2>&1", script_path.c_str());
#endif

    // Execute the script (this triggers transpilation)
    FILE* pipe = popen(command, "r");
    if (!pipe) return "";

    // Read output (we don't need it, but must consume it)
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {}
    pclose(pipe);

    // Read the generated C code from _transpiled_0.c
    std::ifstream transpiled("_transpiled_0.c");
    if (!transpiled.is_open()) {
        return "";
    }

    std::stringstream ss;
    ss << transpiled.rdbuf();
    return ss.str();
}

//==============================================================================
// Pattern Matching Helpers
//==============================================================================

// Check if code contains a pattern
bool contains_pattern(const std::string& code, const std::string& pattern) {
    return code.find(pattern) != std::string::npos;
}

// Count occurrences of a pattern
int count_pattern(const std::string& code, const std::string& pattern) {
    int count = 0;
    size_t pos = 0;
    while ((pos = code.find(pattern, pos)) != std::string::npos) {
        count++;
        pos += pattern.length();
    }
    return count;
}

//==============================================================================
// Parameterized Test Class
//==============================================================================

// Test directories to scan for .transpile fixtures
static const char* TRANSPILE_TEST_DIRECTORIES[] = {
    "test/lambda",
    // Add more directories as needed
};
static const size_t NUM_TRANSPILE_TEST_DIRECTORIES =
    sizeof(TRANSPILE_TEST_DIRECTORIES) / sizeof(TRANSPILE_TEST_DIRECTORIES[0]);

// Discover all transpile tests from all directories
std::vector<TranspileTestInfo> discover_all_transpile_tests() {
    std::vector<TranspileTestInfo> all_tests;

    for (size_t i = 0; i < NUM_TRANSPILE_TEST_DIRECTORIES; i++) {
        std::vector<TranspileTestInfo> dir_tests =
            discover_transpile_tests(TRANSPILE_TEST_DIRECTORIES[i]);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }

    return all_tests;
}

// Global test list - initialized before main via static initialization
static std::vector<TranspileTestInfo>& get_transpile_tests() {
    static std::vector<TranspileTestInfo> tests = discover_all_transpile_tests();
    return tests;
}

class TranspilePatternTest : public ::testing::TestWithParam<TranspileTestInfo> {
};

TEST_P(TranspilePatternTest, VerifyPatterns) {
    const TranspileTestInfo& info = GetParam();

    // Read the fixture file
    std::ifstream fixture_file(info.fixture_path);
    ASSERT_TRUE(fixture_file.is_open())
        << "Failed to open fixture file: " << info.fixture_path;

    std::stringstream fixture_ss;
    fixture_ss << fixture_file.rdbuf();
    std::string fixture_content = fixture_ss.str();

    // Parse the fixture
    TranspileFixture fixture = TranspileFixture::parse(fixture_content);

    // Skip if no patterns defined
    if (fixture.expect.empty() && fixture.forbid.empty()) {
        GTEST_SKIP() << "No patterns defined in fixture: " << info.fixture_path;
    }

    // Transpile the script and get generated code
    std::string code = transpile_and_get_code(info.script_path);
    ASSERT_FALSE(code.empty())
        << "Failed to transpile script or read generated code: " << info.script_path;

    // Check expected patterns
    for (const std::string& pattern : fixture.expect) {
        EXPECT_TRUE(contains_pattern(code, pattern))
            << "Expected pattern not found: \"" << pattern << "\"\n"
            << "Script: " << info.script_path;
    }

    // Check forbidden patterns
    for (const std::string& pattern : fixture.forbid) {
        EXPECT_FALSE(contains_pattern(code, pattern))
            << "Forbidden pattern found: \"" << pattern << "\"\n"
            << "Script: " << info.script_path;
    }
}

// Custom name generator
std::string TranspileTestNameGenerator(const ::testing::TestParamInfo<TranspileTestInfo>& info) {
    return info.param.test_name;
}

// Test suite instantiation using lazy-initialized static vector
INSTANTIATE_TEST_SUITE_P(
    Transpile,
    TranspilePatternTest,
    ::testing::ValuesIn(get_transpile_tests()),
    TranspileTestNameGenerator
);

//==============================================================================
// Main
//==============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    auto& tests = get_transpile_tests();
    if (tests.empty()) {
        printf("No .transpile fixture files found in test directories.\n");
        printf("To add transpile pattern tests, create .transpile files alongside .ls scripts.\n");
        return 0;
    }

    printf("Discovered %zu transpile pattern test(s)\n", tests.size());

    return RUN_ALL_TESTS();
}
