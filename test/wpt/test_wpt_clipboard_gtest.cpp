/**
 * WPT Clipboard API Conformance GTest Runner
 *
 * Discovers WPT Clipboard-API test HTML files in ref/wpt/clipboard-apis/,
 * extracts their <script> blocks (inlining locally-referenced helper JS
 * files like resources/user-activation.js), prepends a testharness.js
 * shim, and executes via lambda.exe js with --document for DOM context.
 *
 * Each test file produces PASS/FAIL counts in the output. Individual
 * failing test cases are reported as GTest failures.
 *
 * NOTE: Lambda's JS DOM does not yet implement the Clipboard API
 * (navigator.clipboard, ClipboardEvent, ClipboardItem, document.execCommand
 * for 'copy'/'cut'/'paste'). Most tests will currently fail with
 * ReferenceError at the first call -- that is expected. This test exists
 * as a tracking baseline so progress can be measured incrementally as
 * the design in vibe/radiant/Radiant_Design_Clipboard.md is implemented.
 *
 * Mirrors test/wpt/test_wpt_selection_gtest.cpp.
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
    #include <sys/stat.h>
#endif

static const char* WPT_DIR = "ref/wpt/clipboard-apis";
static const char* SHIM_PATH = "test/wpt/wpt_testharness_shim.js";
static const char* TEMP_DIR = "temp";

// ---------------------------------------------------------------------------
// Tests deliberately skipped -- they require subsystems Lambda's headless
// js runtime cannot provide (true OS pasteboard injection, real iframes,
// manual user gestures, image/PNG codec, etc.). Track each skip reason here
// so we can revisit as capabilities land.
// ---------------------------------------------------------------------------
static const char* SKIP_SUBSTRINGS[] = {
    // Manual tests -- require user to physically copy/paste.
    "clipboard-file-manual",
    // Detached iframe lifecycle -- depends on real iframe + Document
    // disposal which the headless runner does not model.
    "detached-iframe",
    // Permissions Policy iframe sandboxing -- needs cross-origin iframe.
    "permissions-policy",
    // Image MIME round-trip -- requires PNG codec on pasteboard
    // (Phase 1D in Radiant_Design_Clipboard.md).
    "async-write-image-read-image",
    // SVG read/write -- tentative, depends on SVG-specific pasteboard
    // formats (Phase 2).
    "async-svg-read-write",
    // Custom (web *) MIME formats -- tentative, Phase 2.
    // Tests in this family that DO NOT depend on DOMParser, real fetch,
    // PerformanceObserver, iframe lifecycle, or clipboardchange events
    // have been moved out of this skip list and exercised by the runner.
    "async-navigator-clipboard-read-resource-load",    // needs PerformanceObserver
    "async-navigator-clipboard-write-multiple",        // needs iframes + Actions cancellation
    "async-navigator-clipboard-change-event",          // needs clipboardchange + iframes
    // drag-multiple-urls test depends on real drag and drop event semantics.
    "drag-multiple-urls",
    // DOMParser-based unsanitised HTML round-trip with computed-style
    // expansion -- this test expects browsers to inject the full
    // computed-CSS attribute string (color/font-size/font-style/...);
    // not feasible without a real CSS engine running on the parsed DOM.
    "async-unsanitized-html-formats-write-read",
};
static const int SKIP_COUNT = sizeof(SKIP_SUBSTRINGS) / sizeof(SKIP_SUBSTRINGS[0]);

static bool should_skip(const std::string& name) {
    for (int i = 0; i < SKIP_COUNT; i++) {
        if (name.find(SKIP_SUBSTRINGS[i]) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helpers (mirror test_wpt_selection_gtest.cpp)
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

// extract value of an attribute like src="..." or src=foo.js from a tag string
static std::string extract_attr(const std::string& tag, const char* attr) {
    std::string needle = std::string(attr) + "=";
    size_t p = tag.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    if (p >= tag.size()) return "";
    char quote = tag[p];
    if (quote == '"' || quote == '\'') {
        size_t end = tag.find(quote, p + 1);
        if (end == std::string::npos) return "";
        return tag.substr(p + 1, end - p - 1);
    }
    size_t end = p;
    while (end < tag.size() && tag[end] != ' ' && tag[end] != '\t' &&
           tag[end] != '>' && tag[end] != '/') end++;
    return tag.substr(p, end - p);
}

/**
 * Extract all <script> blocks from an HTML string.
 *
 * Inline scripts are appended as-is. <script src=...> tags are followed
 * when src is a relative path within the test directory (e.g.
 * resources/user-activation.js). External /resources/ paths
 * (testharness.js, testdriver.js, etc.) are skipped -- they are covered
 * by the shim.
 */
static std::string extract_inline_scripts(const std::string& html, const std::string& html_dir) {
    std::string result;
    size_t pos = 0;

    while (pos < html.size()) {
        size_t tag_start = html.find("<script", pos);
        if (tag_start == std::string::npos) break;

        size_t tag_end = html.find('>', tag_start);
        if (tag_end == std::string::npos) break;

        std::string tag = html.substr(tag_start, tag_end - tag_start + 1);
        std::string src = extract_attr(tag, "src");

        if (!src.empty()) {
            std::string body;
            std::string src_label = src;
            if (src[0] != '/' && src.compare(0, 3, "../") != 0 &&
                src.find("://") == std::string::npos) {
                std::string full = html_dir + "/" + src;
                body = read_file_contents(full.c_str());
            }
            if (!body.empty()) {
                result += "// ---- inlined " + src_label + " ----\n";
                result += body;
                result += "\n";
            }
            // External / unresolved srcs are skipped silently.
            pos = tag_end + 1;
            continue;
        }

        size_t close = html.find("</script>", tag_end);
        if (close == std::string::npos) break;

        std::string content = html.substr(tag_end + 1, close - tag_end - 1);
        result += content;
        result += "\n";

        pos = close + strlen("</script>");
    }

    return result;
}

/**
 * Execute a JS file with a document and capture output.
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

struct WptClipboardParam {
    std::string html_path;
    std::string test_name;
    bool        skip;
};

static std::vector<WptClipboardParam> discover_wpt_clipboard_tests() {
    std::vector<WptClipboardParam> params;

#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.html", WPT_DIR);
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern, &fd);
    if (handle == -1) return params;
    do {
        if (fd.attrib & _A_SUBDIR) continue;
        const char* name = fd.name;
#else
    DIR* dir = opendir(WPT_DIR);
    if (!dir) return params;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) continue;
        const char* name = entry->d_name;
#endif

        size_t len = strlen(name);
        if (len < 6 || strcmp(name + len - 5, ".html") != 0) {
            continue;
        }

        std::string base(name, len - 5);

        // Skip reference files
        if (base.size() > 4 && base.substr(base.size() - 4) == "-ref") {
            continue;
        }

        WptClipboardParam p;
        p.html_path = std::string(WPT_DIR) + "/" + name;
        p.test_name = base;
        for (auto& c : p.test_name) {
            if (!isalnum((unsigned char)c)) c = '_';
        }
        p.skip = should_skip(base);
        params.push_back(p);

#ifdef _WIN32
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    }
    closedir(dir);
#endif

    std::sort(params.begin(), params.end(),
              [](const WptClipboardParam& a, const WptClipboardParam& b) {
                  return a.test_name < b.test_name;
              });
    return params;
}

// ---------------------------------------------------------------------------
// Parameterised test
// ---------------------------------------------------------------------------

class WptClipboardTest : public testing::TestWithParam<WptClipboardParam> {};

TEST_P(WptClipboardTest, Run) {
    const auto& p = GetParam();

    if (p.skip) {
        GTEST_SKIP() << "skipped (requires capability not yet supported): " << p.html_path;
    }

    std::string html = read_file_contents(p.html_path.c_str());
    ASSERT_FALSE(html.empty()) << "Could not read test file: " << p.html_path;

    std::string scripts = extract_inline_scripts(html, WPT_DIR);
    if (scripts.empty()) {
        GTEST_SKIP() << "No scripts (reftest or empty): " << p.html_path;
    }

    std::string shim = read_file_contents(SHIM_PATH);
    ASSERT_FALSE(shim.empty()) << "Could not read testharness shim: " << SHIM_PATH;

    // Compose: shim + extracted scripts + onload simulation + summary.
    std::string combined = shim + "\n" + scripts +
                           "\n_wpt_fire_onload();\n_wpt_print_summary();\n";

    std::string temp_js = std::string(TEMP_DIR) + "/wpt_clipboard_" + p.test_name + ".js";
    write_file_contents(temp_js.c_str(), combined);

    int exit_code = 0;
    std::string output = execute_js_with_doc(temp_js.c_str(), p.html_path.c_str(), &exit_code);

    // Parse output for FAIL lines and summary.
    //   FAIL: <name> - <error message>
    //   WPT_RESULT: N/M passed
    std::vector<std::string> failures;
    int pass_count = 0, total_count = 0;

    size_t pos = 0;
    while (pos < output.size()) {
        size_t eol = output.find('\n', pos);
        if (eol == std::string::npos) eol = output.size();
        std::string line = output.substr(pos, eol - pos);
        pos = eol + 1;

        if (line.substr(0, 6) == "FAIL: ") failures.push_back(line);
        if (line.substr(0, 12) == "WPT_RESULT: ") {
            sscanf(line.c_str(), "WPT_RESULT: %d/%d", &pass_count, &total_count);
        }
    }

    unlink(temp_js.c_str());

    // Crash tests pass purely by completing.
    bool is_crash_test = (p.test_name.find("crash") != std::string::npos);
    if (is_crash_test && total_count == 0 && exit_code == 0) {
        printf("  %s: crash test -- completed without crash\n", p.test_name.c_str());
        return;
    }

    if (total_count == 0) {
        // No WPT_RESULT -- script likely failed to load/transpile (e.g. missing
        // navigator.clipboard caused a top-level ReferenceError before any
        // test() call ran). Expected until Phase 1B of
        // vibe/radiant/Radiant_Design_Clipboard.md lands.
        FAIL() << "No test results from " << p.html_path
               << "\nExit code: " << exit_code
               << "\nOutput (first 2KB):\n"
               << output.substr(0, 2048);
        return;
    }

    printf("  %s: %d/%d passed", p.test_name.c_str(), pass_count, total_count);
    if (!failures.empty()) printf(" (%zu failures)", failures.size());
    printf("\n");

    for (const auto& f : failures) {
        ADD_FAILURE() << f;
    }

    EXPECT_EQ(pass_count, total_count)
        << "Not all tests passed in " << p.html_path;
}

INSTANTIATE_TEST_SUITE_P(
    WptClipboard,
    WptClipboardTest,
    testing::ValuesIn(discover_wpt_clipboard_tests()),
    [](const testing::TestParamInfo<WptClipboardParam>& info) {
        return info.param.test_name;
    });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WptClipboardTest);

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
