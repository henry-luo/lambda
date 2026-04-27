/**
 * WPT CSS-Syntax Conformance GTest Runner
 *
 * Discovers WPT css-syntax test HTML files in ref/wpt/css/css-syntax/,
 * extracts their <script> blocks, prepends a testharness.js shim,
 * and executes via lambda.exe js with --document for DOM context.
 *
 * Each test file produces PASS/FAIL counts in the output.
 * Individual failing test cases are reported as GTest failures.
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>
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
    #include <process.h>
    #include <io.h>
    #include <direct.h>
    #define getcwd _getcwd
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

static const char* WPT_DIRS[] = {
    "ref/wpt/css/css-syntax",
    "ref/wpt/css/css-syntax/charset",
};
static const int WPT_DIR_COUNT = sizeof(WPT_DIRS) / sizeof(WPT_DIRS[0]);
static const char* SHIM_PATH = "test/wpt_testharness_shim.js";
static const char* TEMP_DIR = "temp";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_file_contents(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string result(sz, '\0');
    size_t read = fread(&result[0], 1, sz, f);
    result.resize(read);
    fclose(f);
    return result;
}

static void write_file_contents(const char* path, const std::string& content) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
}

/**
 * Extract all <script> block contents from an HTML string.
 * Ignores <script src="..."> tags (external scripts).
 * Returns concatenated inline script content.
 */
static std::string extract_inline_scripts(const std::string& html) {
    std::string result;
    size_t pos = 0;

    while (pos < html.size()) {
        // find <script
        size_t tag_start = html.find("<script", pos);
        if (tag_start == std::string::npos) break;

        // find the closing >
        size_t tag_end = html.find('>', tag_start);
        if (tag_end == std::string::npos) break;

        // check if it has a src= attribute (skip external scripts)
        std::string tag = html.substr(tag_start, tag_end - tag_start + 1);
        if (tag.find("src=") != std::string::npos) {
            pos = tag_end + 1;
            continue;
        }

        // find </script>
        size_t close = html.find("</script>", tag_end);
        if (close == std::string::npos) break;

        // extract content between > and </script>
        std::string content = html.substr(tag_end + 1, close - tag_end - 1);
        result += content;
        result += "\n";

        pos = close + strlen("</script>");
    }

    return result;
}

/**
 * Execute a JS file with a document and capture output.
 * Returns the stdout output and sets exit_code.
 */
static std::string execute_js_with_doc(const char* js_path, const char* html_path, int* exit_code) {
    char command[1024];
#ifdef _WIN32
    snprintf(command, sizeof(command),
             "lambda.exe js \"%s\" --document \"%s\" --no-log 2>&1", js_path, html_path);
#else
    snprintf(command, sizeof(command),
             "./lambda.exe js \"%s\" --document \"%s\" --no-log 2>&1", js_path, html_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        *exit_code = -1;
        return "";
    }

    std::string output;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int raw = pclose(pipe);
    *exit_code = WEXITSTATUS(raw);
    return output;
}

// ---------------------------------------------------------------------------
// Test parameter and discovery
// ---------------------------------------------------------------------------

struct WptCssSyntaxParam {
    std::string html_path;   // path to the .html test file
    std::string test_name;   // sanitised for GTest
};

/**
 * Discover WPT CSS-Syntax test HTML files from all WPT_DIRS.
 * Skips reference files (*-ref.html), support directory, and non-JS tests.
 */
static std::vector<WptCssSyntaxParam> discover_wpt_css_syntax_tests() {
    std::vector<WptCssSyntaxParam> params;

    for (int di = 0; di < WPT_DIR_COUNT; di++) {
        const char* wpt_dir = WPT_DIRS[di];

        // derive a prefix for test names from subdirs beyond the base
        // e.g. "ref/wpt/css/css-syntax/charset" → "charset_"
        std::string prefix;
        std::string base_dir = WPT_DIRS[0];
        std::string cur_dir = wpt_dir;
        if (cur_dir.size() > base_dir.size() + 1) {
            prefix = cur_dir.substr(base_dir.size() + 1);
            for (auto& c : prefix) {
                if (!isalnum((unsigned char)c)) c = '_';
            }
            prefix += "_";
        }

#ifdef _WIN32
        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s\\*.html", wpt_dir);
        struct _finddata_t fd;
        intptr_t handle = _findfirst(pattern, &fd);
        if (handle == -1) continue;
        do {
            if (fd.attrib & _A_SUBDIR) continue;
            const char* name = fd.name;
#else
        DIR* dir = opendir(wpt_dir);
        if (!dir) continue;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR) continue;
            const char* name = entry->d_name;
#endif

            size_t len = strlen(name);
            if (len < 6 || strcmp(name + len - 5, ".html") != 0) {
#ifdef _WIN32
                continue;
#else
                continue;
#endif
            }

            // skip reference files
            std::string base(name, len - 5);
            if (base.size() > 4 && base.substr(base.size() - 4) == "-ref") {
#ifdef _WIN32
                continue;
#else
                continue;
#endif
            }

            // skip xhtml files picked up in charset/ (they have .xhtml extension, but check anyway)

            WptCssSyntaxParam p;
            p.html_path = std::string(wpt_dir) + "/" + name;
            p.test_name = prefix + base;
            for (auto& c : p.test_name) {
                if (!isalnum((unsigned char)c)) c = '_';
            }

            params.push_back(p);

#ifdef _WIN32
        } while (_findnext(handle, &fd) == 0);
        _findclose(handle);
#else
        }
        closedir(dir);
#endif
    } // end for each dir

    std::sort(params.begin(), params.end(),
              [](const WptCssSyntaxParam& a, const WptCssSyntaxParam& b) {
                  return a.test_name < b.test_name;
              });
    return params;
}

// ---------------------------------------------------------------------------
// Parameterised test
// ---------------------------------------------------------------------------

class WptCssSyntaxTest : public testing::TestWithParam<WptCssSyntaxParam> {};

TEST_P(WptCssSyntaxTest, Run) {
    const auto& p = GetParam();

    // Read the HTML test file
    std::string html = read_file_contents(p.html_path.c_str());
    ASSERT_FALSE(html.empty()) << "Could not read test file: " << p.html_path;

    // Extract inline scripts
    std::string scripts = extract_inline_scripts(html);
    if (scripts.empty()) {
        GTEST_SKIP() << "No inline scripts (reftest): " << p.html_path;
    }

    // Read the testharness shim
    std::string shim = read_file_contents(SHIM_PATH);
    ASSERT_FALSE(shim.empty()) << "Could not read testharness shim: " << SHIM_PATH;

    // Compose combined JS: shim + scripts + onload simulation + summary call
    std::string combined = shim + "\n" + scripts + "\n_wpt_fire_onload();\n_wpt_print_summary();\n";

    // Write to temp file
    std::string temp_js = std::string(TEMP_DIR) + "/wpt_" + p.test_name + ".js";
    write_file_contents(temp_js.c_str(), combined);

    // Execute
    int exit_code = 0;
    std::string output = execute_js_with_doc(temp_js.c_str(), p.html_path.c_str(), &exit_code);

    // Parse output for FAIL lines and summary
    // Output format:
    //   FAIL: test name - error message
    //   WPT_RESULT: 5/6 passed

    std::vector<std::string> failures;
    int pass_count = 0, total_count = 0;

    size_t pos = 0;
    while (pos < output.size()) {
        size_t eol = output.find('\n', pos);
        if (eol == std::string::npos) eol = output.size();

        std::string line = output.substr(pos, eol - pos);
        pos = eol + 1;

        if (line.substr(0, 6) == "FAIL: ") {
            failures.push_back(line);
        }
        if (line.substr(0, 12) == "WPT_RESULT: ") {
            // parse "WPT_RESULT: N/M passed"
            sscanf(line.c_str(), "WPT_RESULT: %d/%d", &pass_count, &total_count);
        }
    }

    // Remove temp file
    unlink(temp_js.c_str());

    // Report results
    if (total_count == 0) {
        // No WPT_RESULT line — likely a crash or transpilation error
        FAIL() << "No test results from " << p.html_path
               << "\nExit code: " << exit_code
               << "\nOutput:\n" << output;
        return;
    }

    // Print summary regardless
    printf("  %s: %d/%d passed", p.test_name.c_str(), pass_count, total_count);
    if (!failures.empty()) {
        printf(" (%zu failures)", failures.size());
    }
    printf("\n");

    // Report individual failures as test messages but don't fail the test for now
    // (this allows us to see progress incrementally)
    for (const auto& f : failures) {
        ADD_FAILURE() << f;
    }

    EXPECT_EQ(pass_count, total_count)
        << "Not all tests passed in " << p.html_path;
}

INSTANTIATE_TEST_SUITE_P(
    WptCssSyntax,
    WptCssSyntaxTest,
    testing::ValuesIn(discover_wpt_css_syntax_tests()),
    [](const testing::TestParamInfo<WptCssSyntaxParam>& info) {
        return info.param.test_name;
    });

// Allow graceful skip if test directory is missing
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WptCssSyntaxTest);

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
