/**
 * WPT contenteditable / editing-host Conformance GTest Runner
 *
 * Companion to test_wpt_selection_gtest.cpp, scoped to the editing-host side
 * of the contenteditable work (see vibe/radiant/Radiant_Design_Content_Editable.md
 * §11.1). It pulls in exactly four WPT directories:
 *
 *   - input-events/            §6 InputEvent surface (beforeinput/input,
 *                              getTargetRanges, typing, cut/paste, delete)
 *   - editing/crashtests/      engine robustness (pass by not crashing)
 *   - html/interaction/focus/  implicit focusability of editing hosts
 *   - html/editing/editing-0/  contentEditable/isContentEditable/designMode IDL
 *
 * Each test's <script> blocks are extracted (inlining local helpers and the
 * editing/include helpers), a testharness.js shim is prepended, and the
 * whole thing runs via `lambda.exe js --document` for DOM context.
 *
 * Selection-side editing-host tests (selection/contenteditable/,
 * selection/textcontrols/) already run under test_wpt_selection_gtest.cpp and
 * are NOT duplicated here. The execCommand corpus (editing/run, editing/other,
 * editing/whitespaces, most of editing/plaintext-only) is excluded because
 * Radiant rejects execCommand by design — see the design doc §11.6.
 *
 * Test types within the four dirs:
 *   - testharness tests  -> run, produce N/M assertion counts
 *   - crash tests        -> run (the --document load is the test); pass iff
 *                           the runtime completes without crashing
 *   - reftests / manual  -> skipped (visual-match path / no headless path)
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
#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <thread>

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

static const char* WPT_ROOT = "ref/wpt";
static const char* SHIM_PATH = "test/wpt/wpt_testharness_shim.js";
static const char* TEMP_DIR = "temp";

// The four WPT directories this runner covers (relative to WPT_ROOT). See the
// design doc §11.1 for scope and §11.6 for everything deliberately excluded.
//
// `ce_only` restricts a general-purpose directory to just its
// contenteditable-relevant files. The editing-scoped dirs (input-events,
// editing/crashtests, html/editing/editing-0) take every *.html since the
// whole directory is about editing; html/interaction/focus would otherwise
// drag in ~170 generic focus tests, so only its contenteditable subset is kept.
struct CeTestRoot { const char* path; bool ce_only; };
static const CeTestRoot TEST_ROOTS[] = {
    {"input-events",           false},
    {"editing/crashtests",     false},
    {"html/interaction/focus", true},
    {"html/editing/editing-0", false},
};
static const int TEST_ROOT_COUNT = sizeof(TEST_ROOTS) / sizeof(TEST_ROOTS[0]);

// ---------------------------------------------------------------------------
// Tests we deliberately skip.
// ---------------------------------------------------------------------------
static const char* SKIP_SUBSTRINGS[] = {
    // execCommand is rejected by design (Radiant_Design_Content_Editable.md §9);
    // the one input-events test that drives it can never pass.
    "input-events-exec-command",
    // Manual tests need human gestures; there is no automated headless path.
    "-manual",
};
static const int SKIP_COUNT = sizeof(SKIP_SUBSTRINGS) / sizeof(SKIP_SUBSTRINGS[0]);

static bool should_skip(const std::string& name) {
    for (int i = 0; i < SKIP_COUNT; i++) {
        if (name.find(SKIP_SUBSTRINGS[i]) != std::string::npos) return true;
    }
    return false;
}

// case-insensitive substring search; `needle` must be lowercase
static bool icontains(const std::string& hay, const char* needle) {
    std::string h = hay;
    for (auto& c : h) c = (char)tolower((unsigned char)c);
    return h.find(needle) != std::string::npos;
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

static std::string dir_of(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
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
 * the src is a relative path resolvable against the test file's own directory
 * (html_dir). External WPT resource paths (/resources/testharness.js etc.) are
 * skipped — they are covered by the shim. A small allowlist of known WPT
 * editing helpers (editing/include/editor-test-utils.js, tests.js) is resolved
 * against ref/wpt/ whether referenced absolutely (/editing/include/foo.js) or
 * relatively (../../editing/include/foo.js) — matched on suffix.
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

struct WptCeParam {
    std::string html_path;
    std::string test_name;
    bool        skip;
};

struct WptCeResult {
    WptCeParam param;
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

// Recursively scan `dir` for *.html test files. `rel_prefix` is the path
// relative to WPT_ROOT (e.g. "input-events") and becomes part of the test
// name, so cases across the four roots stay unique and identifiable
// (e.g. "input-events/input-events-typing"). Reference files (*-ref.html) are
// dropped; SKIP_SUBSTRINGS is matched against the relative path.
static void scan_dir(const std::string& dir,
                     const std::string& rel_prefix,
                     bool ce_only,
                     std::vector<WptCeParam>& params) {
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
            scan_dir(dir + "/" + name, rel, ce_only, params);
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
            scan_dir(dir + "/" + name, rel, ce_only, params);
            continue;
        }
#endif
        size_t len = strlen(name);
        if (len < 6 || strcmp(name + len - 5, ".html") != 0) continue;

        std::string rel_base = rel.substr(0, rel.size() - 5);

        // skip reference files
        if (rel_base.size() > 4 &&
            rel_base.substr(rel_base.size() - 4) == "-ref") continue;

        std::string html_path = dir + "/" + name;

        // ce_only roots (general-purpose dirs) keep only contenteditable-
        // relevant files; matches both the `contenteditable` attribute and the
        // `contentEditable`/`isContentEditable` IDL spellings.
        if (ce_only &&
            !icontains(read_file_contents(html_path.c_str()), "contenteditable")) {
            continue;
        }

        WptCeParam p;
        p.html_path = html_path;
        p.test_name = rel_base;
        for (auto& c : p.test_name) {
            if (!isalnum((unsigned char)c)) c = '_';
        }
        p.skip = should_skip(rel_base);
        params.push_back(p);

#ifdef _WIN32
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    }
    closedir(d);
#endif
}

static std::vector<WptCeParam> discover_ce_tests() {
    std::vector<WptCeParam> params;
    for (int i = 0; i < TEST_ROOT_COUNT; i++) {
        std::string full = std::string(WPT_ROOT) + "/" + TEST_ROOTS[i].path;
        scan_dir(full, TEST_ROOTS[i].path, TEST_ROOTS[i].ce_only, params);
    }

    std::sort(params.begin(), params.end(),
              [](const WptCeParam& a, const WptCeParam& b) {
                  return a.test_name < b.test_name;
              });
    return params;
}

// ---------------------------------------------------------------------------
// Parameterised test
// ---------------------------------------------------------------------------

class WptContentEditableTest : public testing::TestWithParam<WptCeParam> {};

static WptCeResult run_ce_case(const WptCeParam& p) {
    WptCeResult result;
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

    // Crash tests (under editing/crashtests/) have no testharness assertions —
    // the --document load itself is the test. Everything else must be a
    // testharness test; reftests / support files (no testharness.js) are
    // skipped (they belong to the layout-compare path, not the JS harness).
    bool is_crash_test = (p.test_name.find("crash") != std::string::npos);
    bool has_testharness = (html.find("testharness.js") != std::string::npos);

    // testdriver tests synthesize input (keyboard / pointer) via test_driver.
    // Phase SI enables narrow keyboard slices as the native headless injector
    // grows; broader structural deletion/pointer matrices remain skipped.
    bool supported_testdriver_case =
        p.html_path.find("contenteditable-false-in-design-mode") != std::string::npos ||
        p.html_path.find("input-events-arrow-key-on-number-input") != std::string::npos ||
        p.html_path.find("input-events-spin-button-click-on-number-input") != std::string::npos ||
        p.html_path.find("input-events-delete-selection") != std::string::npos ||
        p.html_path.find("input-events-get-target-ranges-during-and-after-dispatch") != std::string::npos ||
        p.html_path.find("select-event-drag-remove") < p.html_path.size();
    if (!is_crash_test && !supported_testdriver_case &&
        (html.find("testdriver") != std::string::npos ||
         html.find("test_driver") != std::string::npos)) {
        result.skipped = true;
        result.skip_reason = "requires testdriver synthetic input (Phase SI): " +
                             p.html_path;
        return result;
    }

    if (!is_crash_test && !has_testharness) {
        result.skipped = true;
        result.skip_reason = "non-testharness (reftest / support file): " + p.html_path;
        return result;
    }

    std::string html_dir = dir_of(p.html_path);
    std::string scripts = extract_inline_scripts(html, html_dir);
    if (!is_crash_test && scripts.empty()) {
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
    std::string combined = shim + "\n" + scripts +
                           "\n_wpt_fire_onload();\n_wpt_print_summary();\n";

    std::string temp_js = std::string(TEMP_DIR) + "/wpt_contenteditable_" + p.test_name + ".js";
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

    // Crash tests pass as long as the engine itself does not crash (WPT
    // crash-test semantics). A clean exit (0) OR a normal error exit — e.g.
    // the page references an unimplemented API and the script throws an
    // uncaught exception — means the runtime survived. Only an actual crash
    // signal (SIGSEGV/SIGABRT/…, surfaced by the shell as exit >= 128) fails.
    if (is_crash_test && total_count == 0 && exit_code >= 0 && exit_code < 128) {
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

static void report_gtest_result(const WptCeResult& result) {
    if (result.skipped) {
        GTEST_SKIP() << result.skip_reason;
        return;
    }

    const WptCeParam& p = result.param;
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

TEST_P(WptContentEditableTest, Run) {
    report_gtest_result(run_ce_case(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    WptContentEditable,
    WptContentEditableTest,
    testing::ValuesIn(discover_ce_tests()),
    [](const testing::TestParamInfo<WptCeParam>& info) {
        return info.param.test_name;
    });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WptContentEditableTest);

static int get_parallel_jobs() {
    const char* env_jobs = getenv("LAMBDA_WPT_CONTENTEDITABLE_JOBS");
    if (!env_jobs || !env_jobs[0]) env_jobs = getenv("WPT_CONTENTEDITABLE_JOBS");
    if (env_jobs && env_jobs[0]) {
        int jobs = atoi(env_jobs);
        if (jobs > 0) return jobs;
    }

    unsigned int cpus = std::thread::hardware_concurrency();
    if (cpus <= 1) return 1;
    return (int)cpus - 1;
}

static bool starts_with(const char* text, const char* prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool has_filtered_gtest_arg(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gtest_list_tests") == 0) return true;
        if (starts_with(argv[i], "--gtest_filter=")) {
            const char* filter = argv[i] + strlen("--gtest_filter=");
            if (strcmp(filter, "*") != 0 && strcmp(filter, "WptContentEditable*") != 0)
                return true;
        }
    }
    return false;
}

static std::string get_gtest_json_path(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const char* prefix = "--gtest_output=json:";
        if (starts_with(argv[i], prefix)) {
            return std::string(argv[i] + strlen(prefix));
        }
    }
    return "";
}

static void write_parallel_json(const char* json_path,
                                const std::vector<WptCeResult>& results,
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
        "      \"name\": \"WptContentEditable/WptContentEditableTest\",\n"
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
        const WptCeResult& result = results[i];
        const WptCeParam& p = result.param;
        const char* case_result = result.skipped ? "SKIPPED" : "COMPLETED";
        fprintf(f,
            "        {\n"
            "          \"name\": \"Run/%s\",\n"
            "          \"value_param\": \"%s\",\n"
            "          \"file\": \"%s\",\n"
            "          \"line\": 0,\n"
            "          \"status\": \"RUN\",\n"
            "          \"result\": \"%s\",\n"
            "          \"timestamp\": \"%s\",\n"
            "          \"time\": \"%.3fs\",\n"
            "          \"classname\": \"WptContentEditable/WptContentEditableTest\"",
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
    std::vector<WptCeParam> params = discover_ce_tests();
    std::vector<WptCeResult> results(params.size());
    std::atomic<size_t> next_index(0);
    std::mutex print_mutex;

    int jobs = get_parallel_jobs();
    if (jobs < 1) jobs = 1;
    if ((size_t)jobs > params.size() && !params.empty()) jobs = (int)params.size();

    printf("[==========] Running %zu WPT contenteditable cases with %d jobs\n",
           params.size(), jobs);
    auto started = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (int wi = 0; wi < jobs; wi++) {
        workers.emplace_back([&]() {
            while (true) {
                size_t index = next_index.fetch_add(1);
                if (index >= params.size()) break;

                const WptCeParam& p = params[index];
                {
                    std::lock_guard<std::mutex> lock(print_mutex);
                    printf("[ RUN      ] WptContentEditable/WptContentEditableTest.Run/%s\n",
                           p.test_name.c_str());
                }

                WptCeResult result = run_ce_case(p);
                results[index] = result;

                {
                    std::lock_guard<std::mutex> lock(print_mutex);
                    const char* status = result.skipped ? "[  SKIPPED ]" :
                        (result.failed ? "[  FAILED  ]" : "[       OK ]");
                    printf("%s WptContentEditable/WptContentEditableTest.Run/%s (%.0f ms)\n",
                           status, p.test_name.c_str(), result.seconds * 1000.0);
                }
            }
        });
    }

    for (auto& worker : workers) worker.join();

    auto ended = std::chrono::steady_clock::now();
    double total_seconds = std::chrono::duration<double>(ended - started).count();

    int failures = 0;
    int skipped = 0;
    for (const auto& result : results) {
        if (result.skipped) skipped++;
        else if (result.failed) failures++;
    }

    printf("[==========] %zu WPT contenteditable cases ran. (%.0f ms total)\n",
           params.size(), total_seconds * 1000.0);
    printf("[  PASSED  ] %zu tests.\n", params.size() - failures - skipped);
    if (skipped > 0) printf("[  SKIPPED ] %d tests.\n", skipped);
    if (failures > 0) printf("[  FAILED  ] %d tests.\n", failures);

    std::string json_path = get_gtest_json_path(argc, argv);
    if (!json_path.empty())
        write_parallel_json(json_path.c_str(), results, total_seconds);

    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (!has_filtered_gtest_arg(argc, argv)) {
        return run_parallel_suite(argc, argv);
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
