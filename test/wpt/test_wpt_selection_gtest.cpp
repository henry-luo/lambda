/**
 * WPT Selection API Conformance GTest Runner
 *
 * Discovers WPT Selection-API test HTML files in ref/wpt/selection/,
 * extracts their <script> blocks (inlining locally-referenced helper JS
 * files like common.js / extend.js / addRange.js / collapse.js),
 * prepends a testharness.js shim, and executes via lambda.exe js
 * with --document for DOM context.
 *
 * Each test file produces PASS/FAIL counts in the output.
 * Individual failing test cases are reported as GTest failures.
 *
 * NOTE: Lambda's JS DOM now implements the Selection / Range API
 * (window.getSelection(), document.createRange(), Selection.prototype.*),
 * so these run as genuine conformance tests — a FAIL is a real defect or
 * regression, not a placeholder for a missing API. Tests that need
 * subsystems Lambda does not implement (Shadow DOM, <canvas>, <video>) or
 * synthetic testdriver input it does not yet expose are listed in
 * SKIP_SUBSTRINGS below and reported as SKIPPED.
 *
 * Discovery is recursive: it walks ref/wpt/selection and every
 * subdirectory. Nested cases are named by their path relative to that root
 * (e.g. "contenteditable/collapse"), and a whole subtree can be excluded
 * via SKIP_SUBSTRINGS (e.g. "shadow" skips the shadow-dom subtree).
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
#include <chrono>
#include <ctime>

#include "wpt_parallel_runner.hpp"

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

static const char* WPT_DIR = "ref/wpt/selection";
static const char* SHIM_PATH = "test/wpt/wpt_testharness_shim.js";
static const char* TEMP_DIR = "temp";

// ---------------------------------------------------------------------------
// Tests we deliberately skip — they require subsystems Lambda does not
// implement at all (Shadow DOM, <canvas>, <video>). Everything else now
// runs and any failure shows up as a real FAIL.
// ---------------------------------------------------------------------------
static const char* SKIP_SUBSTRINGS[] = {
    "canvas-",
    "shadow",
    "video",
    // test-iframe.html is a helper file loaded inside iframes by other tests.
    // It contains no testharness assertions of its own and produces 0/0 — not
    // a real test from our perspective.
    "test-iframe",
    // Requires a true iframe JavaScript global/lexical environment. Lambda's
    // current iframe model provides a separate contentDocument, but script
    // helpers still share the parent top-level bindings.
    "deleteFromDocument",
    // dir-manual.html is `setup({explicit_done: true})` and requires the user
    // to click+drag to seed a backward selection before clicking a "Test"
    // button that runs the assertions. There is no automated path: the
    // harness never calls done(), so the test cannot complete in headless.
    "dir-manual",
    // Require WPT testdriver synthetic input (mouse drag / button down-up /
    // keyboard dispatch via test_driver.Actions()) that Lambda's headless
    // `js` runtime does not yet expose. Tracked under Phase 8F; see
    // vibe/radiant/Radiant_Design_Selection2.md.
    "drag-selection-extend-to-user-select-none",
    "onselectstart-on-key-in-contenteditable",
};
static const int SKIP_COUNT = sizeof(SKIP_SUBSTRINGS) / sizeof(SKIP_SUBSTRINGS[0]);

static bool should_skip(const std::string& name) {
    for (int i = 0; i < SKIP_COUNT; i++) {
        if (name.find(SKIP_SUBSTRINGS[i]) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helpers (mirror test_wpt_css_syntax_gtest.cpp)
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
    // unquoted — read until whitespace or >
    size_t end = p;
    while (end < tag.size() && tag[end] != ' ' && tag[end] != '\t' &&
           tag[end] != '>' && tag[end] != '/') end++;
    return tag.substr(p, end - p);
}

/**
 * Extract all <script> blocks from an HTML string.
 * Inline scripts are appended as-is. <script src=...> tags are followed when
 * the src is a relative path within ref/wpt/selection/ (e.g. common.js,
 * extend.js, addRange.js, collapse.js); the referenced file's contents are
 * inlined. External references (e.g. /resources/testharness.js) are skipped
 * — they are covered by the shim.
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
            // Try to inline relative srcs that point to local helpers.
            // Reject anything starting with "/" or "../" — those are WPT
            // resource-server paths (testharness.js etc.) covered by the shim.
            // Exception: a small allowlist of known WPT helper scripts is
            // resolved against ref/wpt/ so tests that include e.g.
            // editing/include/editor-test-utils.js (or tests.js) still get
            // their definitions. These helpers are referenced either
            // absolutely (/editing/include/foo.js) or, from a subdirectory,
            // relatively (../../editing/include/foo.js) — match on suffix so
            // both forms resolve.
            std::string body;
            std::string src_label = src;
            if (src[0] != '/' && src.compare(0, 3, "../") != 0 &&
                src.find("://") == std::string::npos) {
                std::string full = html_dir + "/" + src;
                body = read_file_contents(full.c_str());
            } else {
                static const char* kEditingHelpers[] = {
                    "editing/include/editor-test-utils.js",
                    "editing/include/tests.js",
                };
                for (const char* helper : kEditingHelpers) {
                    size_t hlen = strlen(helper);
                    if (src.size() >= hlen &&
                        src.compare(src.size() - hlen, hlen, helper) == 0) {
                        body = read_file_contents(
                            (std::string("ref/wpt/") + helper).c_str());
                        break;
                    }
                }
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

static std::string extract_inline_only_scripts(const std::string& html) {
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
            pos = tag_end + 1;
            continue;
        }

        size_t close = html.find("</script>", tag_end);
        if (close == std::string::npos) break;

        result += html.substr(tag_end + 1, close - tag_end - 1);
        result += "\n";

        pos = close + strlen("</script>");
    }

    return result;
}

static std::string extract_local_iframe_helper_scripts(const std::string& scripts,
                                                       const std::string& html_dir) {
    std::string result;
    size_t pos = 0;

    while (pos < scripts.size()) {
        size_t src_pos = scripts.find(".src", pos);
        if (src_pos == std::string::npos) break;
        size_t eq = scripts.find('=', src_pos + 4);
        if (eq == std::string::npos) break;
        size_t q = scripts.find_first_of("\"'", eq + 1);
        if (q == std::string::npos) break;
        char quote = scripts[q];
        size_t end = scripts.find(quote, q + 1);
        if (end == std::string::npos) break;

        std::string src = scripts.substr(q + 1, end - q - 1);
        bool local_html = !src.empty() &&
            src[0] != '/' &&
            src.compare(0, 3, "../") != 0 &&
            src.find("://") == std::string::npos &&
            src.size() >= 5 &&
            src.substr(src.size() - 5) == ".html";
        if (local_html) {
            std::string helper_html = read_file_contents((html_dir + "/" + src).c_str());
            if (!helper_html.empty()) {
                result += "// ---- inlined iframe helper " + src + " ----\n";
                result += extract_inline_only_scripts(helper_html);
                result += "\n";
            }
        }
        pos = end + 1;
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

struct WptSelectionParam {
    std::string html_path;
    std::string test_name;
    std::string variant_query;
    bool        skip;
};

struct WptSelectionResult {
    WptSelectionParam param;
    bool skipped;
    bool failed;
    bool crash_test_passed;
    int pass_count;
    int total_count;
    int exit_code;
    double seconds;
    std::string skip_reason;
    std::string output;
    std::vector<std::string> failures;
};

static std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)ch < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)ch);
                    out += buf;
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

static std::string now_timestamp() {
    char buf[32];
    time_t now = time(NULL);
    struct tm tm_now;
#ifdef _WIN32
    gmtime_s(&tm_now, &now);
#else
    gmtime_r(&now, &tm_now);
#endif
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_now);
    return std::string(buf);
}

static void add_selection_param(std::vector<WptSelectionParam>& params,
                                const std::string& path,
                                const std::string& rel_base,
                                const std::string& variant_query) {
    WptSelectionParam p;
    p.html_path = path;
    p.test_name = rel_base;
    if (!variant_query.empty()) {
        p.test_name += "_";
        p.test_name += variant_query.substr(1);
    }
    for (auto& c : p.test_name) {
        if (!isalnum((unsigned char)c)) c = '_';
    }
    p.variant_query = variant_query;
    p.skip = should_skip(rel_base);
    params.push_back(p);
}

static void append_variant_params(std::vector<WptSelectionParam>& params,
                                  const std::string& path,
                                  const std::string& rel_base) {
    std::string html = read_file_contents(path.c_str());
    bool found_variant = false;
    size_t pos = 0;
    while (pos < html.size()) {
        size_t tag_start = html.find("<meta", pos);
        if (tag_start == std::string::npos) break;
        size_t tag_end = html.find('>', tag_start);
        if (tag_end == std::string::npos) break;
        std::string tag = html.substr(tag_start, tag_end - tag_start + 1);
        std::string name = extract_attr(tag, "name");
        if (name == "variant") {
            std::string content = extract_attr(tag, "content");
            if (!content.empty()) {
                add_selection_param(params, path, rel_base, content);
                found_variant = true;
            }
        }
        pos = tag_end + 1;
    }
    if (!found_variant) {
        add_selection_param(params, path, rel_base, "");
    }
}

static std::string js_string_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text) {
        if (ch == '\\' || ch == '"') {
            out += '\\';
            out += ch;
        } else if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else {
            out += ch;
        }
    }
    return out;
}

// Recursively scan `dir` for *.html test files. `rel_prefix` is the path
// relative to WPT_DIR (empty at the top level) and becomes part of the test
// name, so nested cases (e.g. contenteditable/collapse) stay unique and
// identifiable. Subdirectories are traversed; reference files (*-ref.html)
// are dropped, and SKIP_SUBSTRINGS is matched against the relative path so a
// whole subtree (e.g. shadow-dom/) can be skipped by directory name.
static void scan_selection_dir(const std::string& dir,
                               const std::string& rel_prefix,
                               std::vector<WptSelectionParam>& params) {
#ifdef _WIN32
    std::string pattern = dir + "\\*";
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle == -1) return;
    do {
        const char* name = fd.name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        std::string rel = rel_prefix.empty() ? std::string(name)
                                             : rel_prefix + "/" + name;
        if (fd.attrib & _A_SUBDIR) {
            scan_selection_dir(dir + "/" + name, rel, params);
            continue;
        }
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        std::string rel = rel_prefix.empty() ? std::string(name)
                                             : rel_prefix + "/" + name;
        if (entry->d_type == DT_DIR) {
            scan_selection_dir(dir + "/" + name, rel, params);
            continue;
        }
#endif
        size_t len = strlen(name);
        if (len < 6 || strcmp(name + len - 5, ".html") != 0) continue;

        std::string rel_base = rel.substr(0, rel.size() - 5);

        // skip reference files
        if (rel_base.size() > 4 &&
            rel_base.substr(rel_base.size() - 4) == "-ref") continue;

        append_variant_params(params, dir + "/" + name, rel_base);

#ifdef _WIN32
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    }
    closedir(d);
#endif
}

static std::vector<WptSelectionParam> discover_wpt_selection_tests() {
    std::vector<WptSelectionParam> params;
    scan_selection_dir(WPT_DIR, "", params);

    std::sort(params.begin(), params.end(),
              [](const WptSelectionParam& a, const WptSelectionParam& b) {
                  return a.test_name < b.test_name;
              });
    return params;
}

// ---------------------------------------------------------------------------
// Parameterised test
// ---------------------------------------------------------------------------

class WptSelectionTest : public testing::TestWithParam<WptSelectionParam> {};

static WptSelectionResult run_selection_case(const WptSelectionParam& p) {
    WptSelectionResult result;
    result.param = p;
    result.skipped = false;
    result.failed = false;
    result.crash_test_passed = false;
    result.pass_count = 0;
    result.total_count = 0;
    result.exit_code = 0;
    result.seconds = 0.0;

    auto started = std::chrono::steady_clock::now();

    if (p.skip) {
        result.skipped = true;
        result.skip_reason = "skipped (requires capability not yet supported): " +
                             p.html_path;
        return result;
    }

    std::string html = read_file_contents(p.html_path.c_str());
    if (html.empty()) {
        result.failed = true;
        result.failures.push_back("Could not read test file: " + p.html_path);
        return result;
    }
    if (html.find("rel=\"match\"") != std::string::npos ||
        html.find("rel=match") != std::string::npos ||
        html.find("rel='match'") != std::string::npos) {
        result.skipped = true;
        result.skip_reason = "Reftest requires visual comparison: " + p.html_path;
        return result;
    }
    bool supported_testdriver_case =
        p.html_path.find("fire-selectionchange-event-on-deleting-single-character-inside-inline-element") != std::string::npos ||
        p.html_path.find("fire-selectionchange-event-on-pressing-backspace") != std::string::npos ||
        p.html_path.find("fire-selectionchange-event-on-textcontrol-element-on-pressing-backspace") != std::string::npos ||
        p.html_path.find("modifying-selection-with-primary-mouse-button") != std::string::npos ||
        p.html_path.find("modifying-selection-with-non-primary-mouse-button") != std::string::npos ||
        p.html_path.find("selection-direction-on-single-click") != std::string::npos ||
        p.html_path.find("selection-direction-on-double-click") != std::string::npos ||
        p.html_path.find("selection-direction-on-triple-click") != std::string::npos;
    if (!supported_testdriver_case &&
        html.find("testdriver") != std::string::npos) {
        result.skipped = true;
        result.skip_reason = "Requires WPT testdriver synthetic input: " + p.html_path;
        return result;
    }

    std::string scripts = extract_inline_scripts(html, WPT_DIR);
    if (scripts.empty()) {
        result.skipped = true;
        result.skip_reason = "No scripts (reftest or empty): " + p.html_path;
        return result;
    }

    std::string shim = read_file_contents(SHIM_PATH);
    if (shim.empty()) {
        result.failed = true;
        result.failures.push_back(std::string("Could not read testharness shim: ") +
                                  SHIM_PATH);
        return result;
    }

    // Compose: shim + extracted scripts + onload simulation + summary.
    scripts += extract_local_iframe_helper_scripts(scripts, WPT_DIR);
    std::string variant_preamble;
    if (!p.variant_query.empty()) {
        variant_preamble =
            "history.replaceState(null, \"\", \"" +
            js_string_escape(p.variant_query) +
            "\");\n";
    }
    std::string combined = shim + "\n" + variant_preamble + scripts +
                           "\n_wpt_fire_onload();\n_wpt_print_summary();\n";

    std::string temp_js = std::string(TEMP_DIR) + "/wpt_selection_" + p.test_name + ".js";
    write_file_contents(temp_js.c_str(), combined);

    int exit_code = 0;
    std::string output = execute_js_with_doc(temp_js.c_str(), p.html_path.c_str(), &exit_code);
    result.exit_code = exit_code;
    result.output = output;

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

    result.pass_count = pass_count;
    result.total_count = total_count;
    result.failures = failures;

    // WPT "crash tests" (filenames containing "-crash" / "_crash") have no
    // testharness assertions — they pass purely by completing without
    // crashing the runtime. WPT_RESULT: 0/0 + clean exit_code == PASS.
    bool is_crash_test = (p.test_name.find("crash") != std::string::npos);
    if (is_crash_test && total_count == 0 && exit_code == 0) {
        result.crash_test_passed = true;
    } else if (total_count == 0) {
        result.failed = true;
        result.failures.push_back("No test results from " + p.html_path +
            "\nExit code: " + std::to_string(exit_code) +
            "\nOutput (first 2KB):\n" + output.substr(0, 2048));
    } else {
        result.failed = !failures.empty() || pass_count != total_count;
    }

    auto ended = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(ended - started).count();
    return result;
}

static void report_gtest_result(const WptSelectionResult& result) {
    if (result.skipped) {
        GTEST_SKIP() << result.skip_reason;
        return;
    }

    const WptSelectionParam& p = result.param;
    if (result.crash_test_passed) {
        printf("  %s: crash test -- completed without crash\n", p.test_name.c_str());
        return;
    }

    printf("  %s: %d/%d passed", p.test_name.c_str(),
           result.pass_count, result.total_count);
    if (!result.failures.empty()) printf(" (%zu failures)", result.failures.size());
    printf("\n");

    for (const auto& f : result.failures) {
        ADD_FAILURE() << f;
    }

    EXPECT_EQ(result.pass_count, result.total_count)
        << "Not all tests passed in " << p.html_path;
}

TEST_P(WptSelectionTest, Run) {
    report_gtest_result(run_selection_case(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    WptSelection,
    WptSelectionTest,
    testing::ValuesIn(discover_wpt_selection_tests()),
    [](const testing::TestParamInfo<WptSelectionParam>& info) {
        return info.param.test_name;
    });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WptSelectionTest);

static int get_parallel_jobs() {
    return wpt_parallel_jobs("LAMBDA_WPT_SELECTION_JOBS", "WPT_SELECTION_JOBS");
}

static void write_parallel_json(const char* json_path,
                                const std::vector<WptSelectionResult>& results,
                                double total_seconds) {
    if (!json_path || !json_path[0]) return;

    FILE* f = fopen(json_path, "w");
    if (!f) return;

    int failures = 0;
    for (const auto& result : results) {
        if (!result.skipped && result.failed) failures++;
    }

    std::string timestamp = now_timestamp();
    fprintf(f,
        "{\n"
        "  \"tests\": %zu,\n"
        "  \"failures\": %d,\n"
        "  \"disabled\": 0,\n"
        "  \"errors\": 0,\n"
        "  \"timestamp\": \"%s\",\n"
        "  \"time\": \"%.3fs\",\n"
        "  \"name\": \"AllTests\",\n"
        "  \"testsuites\": [\n"
        "    {\n"
        "      \"name\": \"WptSelection/WptSelectionTest\",\n"
        "      \"tests\": %zu,\n"
        "      \"failures\": %d,\n"
        "      \"disabled\": 0,\n"
        "      \"errors\": 0,\n"
        "      \"timestamp\": \"%s\",\n"
        "      \"time\": \"%.3fs\",\n"
        "      \"testsuite\": [\n",
        results.size(), failures, timestamp.c_str(), total_seconds,
        results.size(), failures, timestamp.c_str(), total_seconds);

    for (size_t i = 0; i < results.size(); i++) {
        const WptSelectionResult& result = results[i];
        const WptSelectionParam& p = result.param;
        const char* case_result = result.skipped ? "SKIPPED" : "COMPLETED";
        fprintf(f,
            "        {\n"
            "          \"name\": \"Run/%s\",\n"
            "          \"value_param\": \"%s\",\n"
            "          \"file\": \"%s\",\n"
            "          \"line\": 558,\n"
            "          \"status\": \"RUN\",\n"
            "          \"result\": \"%s\",\n"
            "          \"timestamp\": \"%s\",\n"
            "          \"time\": \"%.3fs\",\n"
            "          \"classname\": \"WptSelection/WptSelectionTest\"",
            json_escape(p.test_name).c_str(),
            json_escape(p.html_path).c_str(),
            json_escape(__FILE__).c_str(),
            case_result,
            timestamp.c_str(),
            result.seconds);

        if (result.skipped) {
            fprintf(f,
                ",\n"
                "          \"skipped\": [ { \"message\": \"%s\" } ]\n"
                "        }%s\n",
                json_escape(result.skip_reason).c_str(),
                (i + 1 == results.size()) ? "" : ",");
        } else if (result.failed) {
            fprintf(f,
                ",\n"
                "          \"failures\": [\n");
            for (size_t fi = 0; fi < result.failures.size(); fi++) {
                fprintf(f,
                    "            { \"failure\": \"%s\", \"type\": \"\" }%s\n",
                    json_escape(result.failures[fi]).c_str(),
                    (fi + 1 == result.failures.size()) ? "" : ",");
            }
            fprintf(f,
                "          ]\n"
                "        }%s\n",
                (i + 1 == results.size()) ? "" : ",");
        } else {
            fprintf(f,
                "\n"
                "        }%s\n",
                (i + 1 == results.size()) ? "" : ",");
        }
    }

    fprintf(f,
        "      ]\n"
        "    }\n"
        "  ]\n"
        "}\n");
    fclose(f);
}

static int run_parallel_suite(int argc, char** argv) {
    std::vector<WptSelectionParam> params = discover_wpt_selection_tests();
    std::vector<WptSelectionResult> results(params.size());
    int jobs = get_parallel_jobs();
    if (jobs < 1) jobs = 1;
    if ((size_t)jobs > params.size() && !params.empty()) jobs = (int)params.size();

    printf("[==========] Running %zu WPT selection cases with %d jobs\n",
           params.size(), jobs);
    auto started = std::chrono::steady_clock::now();

    // WPT cases launch memory-heavy child runtimes, so a shared bounded queue
    // keeps file-level parallelism from multiplying without limit.
    wpt_run_cases_parallel(
        params, results, jobs, run_selection_case,
        [](size_t, const WptSelectionParam& p) {
            printf("[ RUN      ] WptSelection/WptSelectionTest.Run/%s\n",
                   p.test_name.c_str());
        },
        [](size_t, const WptSelectionParam& p, const WptSelectionResult& result) {
            const char* status = result.skipped ? "[  SKIPPED ]" :
                (result.failed ? "[  FAILED  ]" : "[       OK ]");
            printf("%s WptSelection/WptSelectionTest.Run/%s (%.0f ms)\n",
                   status, p.test_name.c_str(), result.seconds * 1000.0);
        });

    auto ended = std::chrono::steady_clock::now();
    double total_seconds = std::chrono::duration<double>(ended - started).count();

    int failures = 0;
    int skipped = 0;
    for (const auto& result : results) {
        if (result.skipped) skipped++;
        else if (result.failed) failures++;
    }

    printf("[==========] %zu WPT selection cases ran. (%.0f ms total)\n",
           params.size(), total_seconds * 1000.0);
    printf("[  PASSED  ] %zu tests.\n", params.size() - failures - skipped);
    if (skipped > 0) printf("[  SKIPPED ] %d tests.\n", skipped);
    if (failures > 0) printf("[  FAILED  ] %d tests.\n", failures);

    std::string json_path = wpt_gtest_json_path(argc, argv);
    if (!json_path.empty())
        write_parallel_json(json_path.c_str(), results, total_seconds);

    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (!wpt_has_filtered_gtest_arg(argc, argv, "WptSelection*")) {
        return run_parallel_suite(argc, argv);
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
