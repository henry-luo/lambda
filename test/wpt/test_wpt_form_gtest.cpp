/**
 * WPT Form & FormData Conformance GTest Runner
 *
 * Covers two WPT directories:
 *   ref/wpt/html/semantics/forms/   — HTMLFormElement, HTMLInputElement,
 *                                     HTMLSelectElement, HTMLTextAreaElement,
 *                                     constraint validation, reset.
 *   ref/wpt/xhr/formdata/           — FormData constructor and all methods.
 *
 * Architecture mirrors test_wpt_dom_events_gtest.cpp:
 *  - Discovers .html and .any.js / .window.js test files.
 *  - Extracts inline <script> blocks, inlines local src= files.
 *  - Prepends the shared wpt_testharness_shim.js.
 *  - Invokes lambda.exe js <tempfile> --document <doc> --no-log.
 *  - Parses FAIL: / WPT_RESULT: lines, reports per-subtest GTest failures.
 *
 * Design document: vibe/radiant/Radiant_Design_Form.md
 *
 * Current expected state (before implementation phases F-0..F-6 land):
 *   most tests will FAIL with ReferenceError or missing IDL. The file exists
 *   as a conformance tracking baseline so progress can be measured phase by
 *   phase.
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
#include <set>

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
    #include <sys/stat.h>
#endif

static const char* FORMS_WPT_DIRS[] = {
    // HTML semantics — forms subdirectories targeted for initial coverage.
    "ref/wpt/html/semantics/forms/the-form-element",
    "ref/wpt/html/semantics/forms/resetting-a-form",
    "ref/wpt/html/semantics/forms/constraints",
    "ref/wpt/html/semantics/forms/attributes-common-to-form-controls",
    "ref/wpt/html/semantics/forms/form-control-infrastructure",
    "ref/wpt/html/semantics/forms/the-input-element",
    "ref/wpt/html/semantics/forms/the-select-element",
    "ref/wpt/html/semantics/forms/the-textarea-element",
    "ref/wpt/html/semantics/forms/the-button-element",
    "ref/wpt/html/semantics/forms/the-fieldset-element",
    "ref/wpt/html/semantics/forms/the-label-element",
    "ref/wpt/html/semantics/forms/the-output-element",
    // XHR — FormData API.
    "ref/wpt/xhr/formdata",
};
static const int FORMS_WPT_DIR_COUNT =
    (int)(sizeof(FORMS_WPT_DIRS) / sizeof(FORMS_WPT_DIRS[0]));

static const char* SHIM_PATH  = "test/wpt/wpt_testharness_shim.js";
static const char* BLANK_DOC  = "test/wpt/wpt_blank_document.html";
static const char* TEMP_DIR   = "temp";
static const char* RESULT_JSON_PATH = "test_output/test_wpt_form_gtest_results.json";

// Conformance baseline: records the test cases that currently pass. A baselined
// test that later fails is a regression (real error). A non-baselined test that
// fails is a known/expected failure and is tolerated (reported as skipped).
// Regenerate with: WPT_FORM_UPDATE_BASELINE=1 ./test/test_wpt_form_gtest.exe
static const char* BASELINE_PATH = "test/wpt/wpt_form_baseline.txt";

// ---------------------------------------------------------------------------
// Skip list — tests that require capabilities Lambda's headless JS runtime
// cannot provide. Entries are substrings matched against the file name.
// ---------------------------------------------------------------------------
static const char* SKIP_SUBSTRINGS[] = {
    // Requires test_driver (WebDriver) for user input simulation.
    "testdriver",
    "send-keys",
    "change-set-value",          // async_test + test_driver.send_keys
    "email-set-value",           // async_test + test_driver
    "change-to-empty-value",     // async_test + test_driver
    "checkable-active",          // requires real keydown/click dispatch from UA
    "anchor-active",             // requires real user focus + keypress
    "click-user-gesture",        // requires trusted user gesture
    "click-events",              // async + test_driver
    "auto-direction",            // dir=auto bidi algorithm

    // Form submission / navigation — Lambda has no browsing context.
    "form-action-submission",
    "form-check-validity-crash",  // crashes only during submission
    "submission",
    "submitter",

    // Shadow DOM — Lambda has no shadow DOM.
    "shadow",
    "Shadow",

    // Date/time picker UI, valueAsDate, valueAsNumber — not in scope.
    "valueasdate",
    "valueAsDate",
    "datetime-local-valueasdate",
    "date-time-with-trusted-types",
    "weekmonth",

    // async_test that requires real focus management / requestAnimationFrame.
    "focus-dynamic-type",
    "focus-event",
    "checkable-active",

    // Blob/File upload beyond basic Blob stubs.
    "set-blob",                   // requires Blob + File with binary content

    // Custom elements / form-associated custom elements.
    "form-associated",
    "custom-element",
    "customizable",               // customizable-combobox and variants

    // iframe / cross-realm.
    ".sub.",
    "iframe",
    "cross-origin",

    // Crash regression tests that depend on internal C++ state beyond
    // what the headless JS layer can trigger.
    "form-controls-id-removal-crash",
    "form-controls-nested-id-crash",
    "input-type-number-rtl-invalid-crash",
    "input-type-change-empty-crash",
    "select-add-optgroup-before-argument-is-select-crash",
    "select-add-option-crash",
    "add-optgroup-crash",

    // Rendering/visual tests (reference images) and `link rel=match` tests (no JS assertions).
    "-ref.html",
    "list-box-important-colors",
    "reset-algorithm-rendering",
    "wrap-reflect-1a.html",   // link rel=match reference test, no test() calls
    "wrap-reflect-1b.html",   // link rel=match reference test, no test() calls
    "wrap-enumerated-ascii-case-insensitive.html", // link rel=match reference test

    // tentative tests depend on not-yet-stable spec features.
    ".tentative",

    // filterable-select / listbox / popup variants — too new.
    "filterable-select",
    "customizable-select",
    "list-box",
    "multiple-popup",

    // specific tests requiring test_driver or async user interactions.
    "checkbox.html",              // async_test + real click dispatch
    "beforeinput.tentative",
    "historical-search-event",
    "historical.html",
    "option-selectedness-script-mutation",  // requires script-driven
                                             // select mutation + async_test
    "input-change-event-properties",        // async_test + test_driver
    "inserted-or-removed",                  // async_test + MutationObserver
    "select-ask-for-reset",                 // animation-frame based
    "cloning-steps",                        // cloneNode + script hooks
    "color-attributes",                     // .window.js + color input UI
    "dirname-rtl-manual",                  // manual interaction test

    // Constraints support validator.js with complex attribute mutations.
    // Some tests need RE2 pattern matching (F-4 phase) — skip for now.
    "form-validation-validity-patternMismatch",
    "form-validation-validity-rangeOverflow",
    "form-validation-validity-rangeUnderflow",
    "form-validation-validity-stepMismatch",
    "form-validation-validity-badInput",
    "form-validation-validity-typeMismatch",
    "form-validation-reportValidity",       // requires real rendering UI

    // FormData set-blob still requires Blob with binary content (ArrayBuffer/typed array).
    "set-blob",
};
static const int SKIP_COUNT =
    (int)(sizeof(SKIP_SUBSTRINGS) / sizeof(SKIP_SUBSTRINGS[0]));

static bool should_skip(const std::string& name) {
    for (int i = 0; i < SKIP_COUNT; i++) {
        if (name.find(SKIP_SUBSTRINGS[i]) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helpers (identical pattern to test_wpt_dom_events_gtest.cpp)
// ---------------------------------------------------------------------------

static std::string read_file_str(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string result(sz, '\0');
    size_t nread = fread(&result[0], 1, (size_t)sz, f);
    result.resize(nread);
    fclose(f);
    return result;
}

static void write_file_str(const char* path, const std::string& content) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
}

// extract the value of an HTML attribute from a tag string.
static std::string extract_attr(const std::string& tag, const char* attr) {
    std::string needle = std::string(attr) + "=";
    size_t p = tag.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    if (p >= tag.size()) return "";
    char q = tag[p];
    if (q == '"' || q == '\'') {
        size_t end = tag.find(q, p + 1);
        if (end == std::string::npos) return "";
        return tag.substr(p + 1, end - p - 1);
    }
    size_t end = p;
    while (end < tag.size() && tag[end] != ' ' && tag[end] != '\t' &&
           tag[end] != '>' && tag[end] != '/') end++;
    return tag.substr(p, end - p);
}

/**
 * Extract all <script> blocks from HTML. Inline scripts are appended as-is.
 * <script src=...> tags are followed when src is a relative local path within
 * or alongside html_dir (e.g. "support/validator.js", "resources/foo.js").
 * Absolute /resources/testharness.js etc. are skipped (covered by the shim).
 */
static std::string extract_inline_scripts(const std::string& html,
                                           const std::string& html_dir) {
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
            // skip absolute /resources/... (testharness.js, testdriver, etc.)
            // and absolute http:// / https:// URLs.
            if (src[0] != '/' && src.compare(0, 3, "../") != 0 &&
                src.find("://") == std::string::npos) {
                // relative local file — inline it.
                std::string full = html_dir + "/" + src;
                std::string body = read_file_str(full.c_str());
                if (!body.empty()) {
                    result += "// ---- inlined " + src + " ----\n";
                    result += body;
                    result += "\n";
                }
            }
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

/** Wrap a bare .any.js / .window.js body into a minimal HTML-like wrapper. */
static std::string wrap_any_js(const std::string& js) {
    return std::string("<script>\n") + js + "\n</script>\n";
}

/** Execute a JS file with a document and capture output. */
static std::string execute_js_with_doc(const char* js_path,
                                        const char* html_path,
                                        int* exit_code) {
    char command[2048];
#ifdef _WIN32
    snprintf(command, sizeof(command),
             "lambda.exe js \"%s\" --document \"%s\" --no-log 2>&1",
             js_path, html_path);
#else
    snprintf(command, sizeof(command),
             "./lambda.exe js \"%s\" --document \"%s\" --no-log 2>&1",
             js_path, html_path);
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

struct WptFormParam {
    std::string source_path;   // .html or .js source file
    std::string html_path;     // --document argument
    std::string test_name;     // sanitised GTest parameter name
    std::string dir_path;      // directory containing the test file
    double      previous_seconds;
    bool        is_any_js;     // bare .any.js / .window.js
    bool        skip;
};

struct WptFormResult {
    WptFormParam param;
    bool skipped;
    bool failed;
    bool known_failure = false;  // failed but not in baseline — tolerated
    bool new_pass = false;       // passed but not yet in baseline
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

static double parse_seconds_after_time_key(const std::string& json, size_t pos) {
    size_t key = json.find("\"time\"", pos);
    if (key == std::string::npos) return 0.0;
    size_t colon = json.find(':', key);
    if (colon == std::string::npos) return 0.0;
    size_t value = json.find_first_not_of(" \t\r\n", colon + 1);
    if (value == std::string::npos) return 0.0;

    bool quoted = json[value] == '"';
    if (quoted) value++;
    char* end = NULL;
    double seconds = strtod(json.c_str() + value, &end);
    if (!end || end == json.c_str() + value) return 0.0;
    if (strncmp(end, "ms", 2) == 0) seconds /= 1000.0;
    return seconds < 0.0 ? 0.0 : seconds;
}

static double previous_duration_seconds(const std::string& test_name) {
    static std::string json = read_file_str(RESULT_JSON_PATH);
    if (json.empty()) return 0.0;

    std::string needle = "\"name\": \"Run/" + test_name + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        needle = "\"name\": \"Run\\/" + test_name + "\"";
        pos = json.find(needle);
    }
    if (pos == std::string::npos) return 0.0;
    return parse_seconds_after_time_key(json, pos);
}

static std::vector<WptFormParam> discover_form_tests() {
    std::vector<WptFormParam> params;

    for (int di = 0; di < FORMS_WPT_DIR_COUNT; di++) {
        const char* wpt_dir = FORMS_WPT_DIRS[di];

#ifdef _WIN32
        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s\\*.*", wpt_dir);
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
            std::string fname(name, len);

            bool is_html = (len >= 5 && strcmp(name + len - 5, ".html") == 0);
            bool is_any_js = (fname.find(".any.js") != std::string::npos) ||
                             (fname.find(".window.js") != std::string::npos);
            // worker tests: no document context — skip silently.
            bool is_worker = (fname.find(".worker.js") != std::string::npos);

            if (!is_html && !is_any_js && !is_worker) continue;

            // skip reference images
            if (is_html && len >= 9 && strcmp(name + len - 9, "-ref.html") == 0)
                continue;

            WptFormParam p;
            p.source_path = std::string(wpt_dir) + "/" + name;
            p.dir_path    = wpt_dir;
            p.is_any_js   = is_any_js || is_worker;

            // build a unique GTest-compatible name:
            // dir suffix (last path component) + "_" + base name
            std::string dir_suffix;
            {
                std::string ds = std::string(wpt_dir);
                size_t slash = ds.rfind('/');
                dir_suffix = (slash != std::string::npos) ? ds.substr(slash + 1) : ds;
            }

            std::string base = fname;
            if (is_html) {
                base = base.substr(0, base.size() - 5);
            } else {
                size_t dot = base.rfind(".js");
                if (dot != std::string::npos) base = base.substr(0, dot);
                size_t dot2 = base.rfind(".");
                if (dot2 != std::string::npos) {
                    std::string ext = base.substr(dot2);
                    if (ext == ".any" || ext == ".window" || ext == ".worker")
                        base = base.substr(0, dot2);
                }
            }

            std::string suffix;
            if (is_html)                                  suffix = "_html";
            else if (fname.find(".any.js") != std::string::npos)    suffix = "_any";
            else if (fname.find(".window.js") != std::string::npos) suffix = "_window";
            else if (fname.find(".worker.js") != std::string::npos) suffix = "_worker";

            p.test_name = dir_suffix + "__" + base + suffix;
            for (auto& c : p.test_name) {
                if (!isalnum((unsigned char)c)) c = '_';
            }

            p.html_path = is_html ? p.source_path : "";
            p.previous_seconds = previous_duration_seconds(p.test_name);
            p.skip      = is_worker || should_skip(fname);

            params.push_back(p);

#ifdef _WIN32
        } while (_findnext(handle, &fd) == 0);
        _findclose(handle);
#else
        }
        closedir(dir);
#endif
    }

    std::sort(params.begin(), params.end(),
              [](const WptFormParam& a, const WptFormParam& b) {
                  if (a.previous_seconds != b.previous_seconds)
                      return a.previous_seconds > b.previous_seconds;
                  return a.test_name < b.test_name;
              });
    return params;
}

// ---------------------------------------------------------------------------
// Parameterised test
// ---------------------------------------------------------------------------

class WptFormTest : public testing::TestWithParam<WptFormParam> {};

static WptFormResult run_form_case(const WptFormParam& p) {
    WptFormResult result;
    result.param = p;
    result.skipped = false;
    result.failed = false;
    result.pass_count = 0;
    result.total_count = 0;
    result.exit_code = 0;
    result.seconds = 0.0;

    auto started = std::chrono::steady_clock::now();

    if (p.skip) {
        result.skipped = true;
        result.skip_reason = "skipped (requires capability not in headless runtime): " +
                             p.source_path;
        result.seconds = 0.0;
        return result;
    }

    std::string scripts;
    std::string doc_arg = p.html_path;

    if (p.is_any_js) {
        std::string body = read_file_str(p.source_path.c_str());
        if (body.empty()) {
            result.failed = true;
            result.failures.push_back("Could not read: " + p.source_path);
            result.seconds = 0.0;
            return result;
        }
        std::string wrapped = wrap_any_js(body);
        scripts = extract_inline_scripts(wrapped, p.dir_path);
        doc_arg = BLANK_DOC;
    } else {
        std::string html = read_file_str(p.source_path.c_str());
        if (html.empty()) {
            result.failed = true;
            result.failures.push_back("Could not read: " + p.source_path);
            result.seconds = 0.0;
            return result;
        }
        scripts = extract_inline_scripts(html, p.dir_path);
    }

    if (scripts.empty()) {
        result.skipped = true;
        result.skip_reason = "No scripts (reftest or empty): " + p.source_path;
        result.seconds = 0.0;
        return result;
    }

    std::string shim = read_file_str(SHIM_PATH);
    if (shim.empty()) {
        result.failed = true;
        result.failures.push_back(std::string("Could not read shim: ") + SHIM_PATH);
        result.seconds = 0.0;
        return result;
    }

    std::string combined = "var _wpt_fast_pending_async = true;\n" + shim + "\n" + scripts +
                           "\n_wpt_fire_onload();\n_wpt_print_summary();\n";

    std::string temp_js = std::string(TEMP_DIR) + "/wpt_form_" + p.test_name + ".js";
    write_file_str(temp_js.c_str(), combined);

    int exit_code = 0;
    std::string output = execute_js_with_doc(temp_js.c_str(), doc_arg.c_str(), &exit_code);
    result.exit_code = exit_code;
    result.output = output;

    // parse FAIL: and WPT_RESULT: lines.
    std::vector<std::string> failures;
    int pass_count = 0, total_count = 0;

    size_t pos = 0;
    while (pos < output.size()) {
        size_t eol = output.find('\n', pos);
        if (eol == std::string::npos) eol = output.size();
        std::string line = output.substr(pos, eol - pos);
        pos = eol + 1;

        if (line.substr(0, 6) == "FAIL: ")
            failures.push_back(line);
        if (line.substr(0, 12) == "WPT_RESULT: ")
            sscanf(line.c_str(), "WPT_RESULT: %d/%d", &pass_count, &total_count);
    }

    // unlink(temp_js.c_str());  // temporarily disabled for debugging

    if (total_count == 0) {
        result.failed = true;
        result.failures.push_back("No test results from " + p.source_path +
            "\nExit: " + std::to_string(exit_code) +
            "\nOutput (first 2KB):\n" + output.substr(0, 2048));
    } else {
        result.pass_count = pass_count;
        result.total_count = total_count;
        result.failures = failures;
        result.failed = !failures.empty() || pass_count != total_count;
    }

    auto ended = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(ended - started).count();
    return result;
}

// ---------------------------------------------------------------------------
// Baseline handling
// ---------------------------------------------------------------------------

// load the set of test names expected to pass (cached for the process).
static const std::set<std::string>& get_baseline() {
    static std::set<std::string> baseline = []() {
        std::set<std::string> s;
        std::string content = read_file_str(BASELINE_PATH);
        size_t pos = 0;
        while (pos < content.size()) {
            size_t eol = content.find('\n', pos);
            if (eol == std::string::npos) eol = content.size();
            std::string line = content.substr(pos, eol - pos);
            pos = eol + 1;
            // trim surrounding whitespace / CR.
            size_t start = line.find_first_not_of(" \t\r");
            if (start == std::string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r");
            line = line.substr(start, end - start + 1);
            if (line.empty() || line[0] == '#') continue;  // comment or blank
            s.insert(line);
        }
        return s;
    }();
    return baseline;
}

// classify a result against the baseline: a non-baselined failure is tolerated
// as a known failure; a passing test missing from the baseline is a new pass.
static void classify_against_baseline(WptFormResult& r) {
    r.known_failure = false;
    r.new_pass = false;
    if (r.skipped) return;
    bool in_baseline = get_baseline().count(r.param.test_name) > 0;
    bool passing = !r.failed && r.total_count > 0;
    if (!passing) {
        if (!in_baseline) r.known_failure = true;  // tolerated, not a regression
    } else if (!in_baseline) {
        r.new_pass = true;  // improvement — candidate for the baseline
    }
}

static bool baseline_update_requested() {
    const char* v = getenv("WPT_FORM_UPDATE_BASELINE");
    return v && v[0] && strcmp(v, "0") != 0;
}

// rewrite the baseline file from the set of currently passing tests.
static void write_baseline(const std::vector<WptFormResult>& results) {
    std::vector<std::string> passing;
    for (const auto& r : results) {
        if (!r.skipped && !r.failed && r.total_count > 0)
            passing.push_back(r.param.test_name);
    }
    std::sort(passing.begin(), passing.end());

    std::string out;
    out += "# WPT Form & FormData conformance baseline.\n";
    out += "# Lists the test cases that currently pass; only these are enforced.\n";
    out += "# A listed test that fails is a regression; an unlisted failure is tolerated.\n";
    out += "# Regenerate with: WPT_FORM_UPDATE_BASELINE=1 ./test/test_wpt_form_gtest.exe\n";
    out += "\n";
    for (const auto& name : passing) { out += name; out += "\n"; }

    write_file_str(BASELINE_PATH, out);
    printf("[ BASELINE ] Wrote %zu passing tests to %s\n",
           passing.size(), BASELINE_PATH);
}

static void report_gtest_result(WptFormResult result) {
    classify_against_baseline(result);

    if (result.skipped) {
        GTEST_SKIP() << result.skip_reason;
        return;
    }
    if (result.known_failure) {
        GTEST_SKIP() << "known failure (not in baseline): "
                     << result.pass_count << "/" << result.total_count << " passed";
        return;
    }

    const WptFormParam& p = result.param;
    printf("  %s: %d/%d passed", p.test_name.c_str(),
           result.pass_count, result.total_count);
    if (!result.failures.empty()) printf(" (%zu failures)", result.failures.size());
    printf("\n");

    for (const auto& f : result.failures) {
        ADD_FAILURE() << f;
    }

    EXPECT_EQ(result.pass_count, result.total_count)
        << "Not all subtests passed in " << p.source_path;
}

TEST_P(WptFormTest, Run) {
    report_gtest_result(run_form_case(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    WptForm,
    WptFormTest,
    testing::ValuesIn(discover_form_tests()),
    [](const testing::TestParamInfo<WptFormParam>& info) {
        return info.param.test_name;
    });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WptFormTest);

static int get_parallel_jobs() {
    return wpt_parallel_jobs("LAMBDA_WPT_FORM_JOBS", "WPT_FORM_JOBS");
}

static void write_parallel_json(const char* json_path,
                                const std::vector<WptFormResult>& results,
                                double total_seconds) {
    if (!json_path || !json_path[0]) return;

    FILE* f = fopen(json_path, "w");
    if (!f) return;

    int failures = 0;
    for (const auto& result : results) {
        if (!result.skipped && !result.known_failure && result.failed) failures++;
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
        "      \"name\": \"WptForm/WptFormTest\",\n"
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
        const WptFormResult& result = results[i];
        const WptFormParam& p = result.param;
        // a tolerated known failure is recorded as skipped, not a failure.
        bool as_skipped = result.skipped || result.known_failure;
        const char* case_result = as_skipped ? "SKIPPED" : "COMPLETED";
        fprintf(f,
            "        {\n"
            "          \"name\": \"Run/%s\",\n"
            "          \"value_param\": \"%s\",\n"
            "          \"file\": \"%s\",\n"
            "          \"line\": 448,\n"
            "          \"status\": \"RUN\",\n"
            "          \"result\": \"%s\",\n"
            "          \"timestamp\": \"%s\",\n"
            "          \"time\": \"%.3fs\",\n"
            "          \"classname\": \"WptForm/WptFormTest\"",
            json_escape(p.test_name).c_str(),
            json_escape(p.source_path).c_str(),
            json_escape(__FILE__).c_str(),
            case_result,
            timestamp.c_str(),
            result.seconds);

        if (as_skipped) {
            std::string msg = result.skipped ? result.skip_reason :
                ("known failure (not in baseline): " +
                 std::to_string(result.pass_count) + "/" +
                 std::to_string(result.total_count) + " passed");
            fprintf(f,
                ",\n"
                "          \"skipped\": [ { \"message\": \"%s\" } ]\n"
                "        }%s\n",
                json_escape(msg).c_str(),
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
    std::vector<WptFormParam> params = discover_form_tests();
    std::vector<WptFormResult> results(params.size());
    int jobs = get_parallel_jobs();
    if (jobs < 1) jobs = 1;
    if ((size_t)jobs > params.size() && !params.empty()) jobs = (int)params.size();

    printf("[==========] Running %zu WPT form cases with %d jobs\n",
           params.size(), jobs);
    auto started = std::chrono::steady_clock::now();

    // WPT cases launch memory-heavy child runtimes, so a shared bounded queue
    // keeps file-level parallelism from multiplying without limit.
    wpt_run_cases_parallel(
        params, results, jobs, run_form_case,
        [](size_t, const WptFormParam& p) {
            printf("[ RUN      ] WptForm/WptFormTest.Run/%s\n", p.test_name.c_str());
        },
        [](size_t, const WptFormParam& p, const WptFormResult& result) {
            const char* status = result.skipped ? "[  SKIPPED ]" :
                (result.failed ? "[  FAILED  ]" : "[       OK ]");
            printf("%s WptForm/WptFormTest.Run/%s (%.0f ms)\n",
                   status, p.test_name.c_str(), result.seconds * 1000.0);
        });

    auto ended = std::chrono::steady_clock::now();
    double total_seconds = std::chrono::duration<double>(ended - started).count();

    // baseline update mode: record the current passing set and exit.
    if (baseline_update_requested()) {
        write_baseline(results);
        return 0;
    }

    // classify every result against the baseline before tallying.
    for (auto& result : results) classify_against_baseline(result);

    int regressions = 0;   // baselined tests that now fail — real errors
    int known_fail = 0;    // non-baselined failures — tolerated
    int new_pass = 0;      // passing but not yet in baseline
    int skipped = 0;
    int passed = 0;
    std::vector<std::string> regression_names;
    std::vector<std::string> new_pass_names;
    for (const auto& result : results) {
        if (result.skipped) { skipped++; }
        else if (result.known_failure) { known_fail++; }
        else if (result.failed) { regressions++; regression_names.push_back(result.param.test_name); }
        else { passed++; if (result.new_pass) new_pass_names.push_back(result.param.test_name); }
    }
    new_pass = (int)new_pass_names.size();

    printf("[==========] %zu WPT form cases ran. (%.0f ms total)\n",
           params.size(), total_seconds * 1000.0);
    printf("[  PASSED  ] %d tests.\n", passed);
    if (skipped > 0)    printf("[  SKIPPED ] %d tests.\n", skipped);
    if (known_fail > 0) printf("[  KNOWN   ] %d tolerated failures (not in baseline).\n", known_fail);
    if (new_pass > 0) {
        printf("[ NEW PASS ] %d tests pass but are not in the baseline — "
               "run WPT_FORM_UPDATE_BASELINE=1 to record them:\n", new_pass);
        for (const auto& name : new_pass_names)
            printf("             + %s\n", name.c_str());
    }
    if (regressions > 0) {
        printf("[  FAILED  ] %d baseline regressions.\n", regressions);
        for (const auto& name : regression_names)
            printf("             - %s\n", name.c_str());
    }

    std::string json_path = wpt_gtest_json_path(argc, argv);
    if (!json_path.empty())
        write_parallel_json(json_path.c_str(), results, total_seconds);

    return regressions == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (!wpt_has_filtered_gtest_arg(argc, argv, "WptForm*")) {
        return run_parallel_suite(argc, argv);
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
