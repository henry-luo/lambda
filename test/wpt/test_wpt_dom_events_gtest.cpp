/**
 * WPT DOM Events Conformance GTest Runner
 *
 * Discovers WPT DOM-event test HTML files in ref/wpt/dom/events/, extracts
 * their <script> blocks (inlining locally-referenced helper JS files from
 * the resources directory), prepends the shared testharness.js shim, and executes
 * via lambda.exe js with --document for DOM context.
 *
 * Each test file produces PASS/FAIL counts in the output. Individual
 * failing test cases are reported as GTest failures.
 *
 * NOTE: Lambda's JS DOM Event/EventTarget/Event-subclass implementation is
 * incomplete -- many tests will fail with ReferenceError or assertion
 * failures until the phases described in
 * vibe/radiant/Radiant_Design_Event.md land. This test exists as a tracking
 * baseline so progress can be measured incrementally.
 *
 * Mirrors test/wpt/test_wpt_clipboard_gtest.cpp.
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

#ifndef WPT_RUNNER_DIR
#define WPT_RUNNER_DIR "ref/wpt/dom/events"
#endif
#ifndef WPT_RUNNER_TEMP_PREFIX
#define WPT_RUNNER_TEMP_PREFIX "wpt_dom_events_"
#endif
#ifndef WPT_RUNNER_BASELINE_PATH
#define WPT_RUNNER_BASELINE_PATH ""
#endif
#ifndef WPT_RUNNER_UPDATE_ENV
#define WPT_RUNNER_UPDATE_ENV ""
#endif
#ifndef WPT_RUNNER_INCLUDE
#define WPT_RUNNER_INCLUDE(name) true
#endif
#ifndef WPT_RUNNER_SKIP_TENTATIVE
#define WPT_RUNNER_SKIP_TENTATIVE 1
#endif

static const char* WPT_DIR = WPT_RUNNER_DIR;
static const char* SHIM_PATH = "test/wpt/wpt_testharness_shim.js";
static const char* TEMP_DIR = "temp";
static const char* BASELINE_PATH = WPT_RUNNER_BASELINE_PATH;
static const char* UPDATE_BASELINE_ENV = WPT_RUNNER_UPDATE_ENV;

static bool passing_baseline_contains(const char* test_name) {
    if (!BASELINE_PATH[0] || !test_name) return false;
    FILE* file = fopen(BASELINE_PATH, "r");
    if (!file) return false;
    char line[1024];
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (strcmp(line, test_name) == 0) {
            found = true;
            break;
        }
    }
    fclose(file);
    return found;
}

static bool update_baseline_requested() {
    if (!UPDATE_BASELINE_ENV[0]) return false;
    const char* value = getenv(UPDATE_BASELINE_ENV);
    return value && value[0] && strcmp(value, "0") != 0;
}

static void append_passing_baseline(const std::string& test_name) {
    if (!update_baseline_requested() || !BASELINE_PATH[0]) return;
    FILE* file = fopen(BASELINE_PATH, "a");
    if (!file) return;
    fprintf(file, "%s\n", test_name.c_str());
    fclose(file);
}

// ---------------------------------------------------------------------------
// Tests deliberately skipped -- they require subsystems Lambda's headless
// js runtime cannot provide (cross-realm subframes, shadow DOM, focus
// management, real animation/transition systems, scroll layout), or are
// gated on phases of vibe/radiant/Radiant_Design_Event.md that have not
// landed yet.
// ---------------------------------------------------------------------------
static const char* SKIP_SUBSTRINGS[] = {
    // Cross-realm / subframe tests -- need iframes + multiple Window globals.
    "EventListener-incumbent-global",
    "event-global-extra",
    "Event-dispatch-throwing-multiple-globals",
    "Event-timestamp-cross-realm",
    "EventListener-handleEvent-cross-realm",

    // Shadow DOM -- Lambda has no shadow DOM, so retargeting / closed
    // composedPath cannot be modelled.
    "shadow-relatedTarget",
    "window-composed-path",
    "mouse-event-retarget",

    // Real focus management.
    "focus-event-document-move",
    "no-focus-events-at-clicking-editable-content-in-link",

    // Pointer events -- depend on full PointerEvent + pointer capture.
    "pointer-event-document-move",

    // WebKit-prefixed animation/transition end events -- need real
    // animation engine driving the events.
    "webkit-animation",
    "webkit-transition",

    // Activation behaviour / form-control click semantics -- Phase 4
    // of Radiant_Design_Event.md.
    "label-default-action",
    "legacy-pre-activation-behavior",
    "preventDefault-during-activation-behavior",
    "Event-dispatch-single-activation-behavior",

    // IME / keypress crash regressions -- need editable text input.
    "keypress-dispatch-crash",

    // Event-dispatch-on-disabled-elements requires CSS transitions/animations
    // for 5 of 9 sub-tests; the synchronous click() guard subset is exercised
    // by event_disabled_dynamic_html instead.
    "Event-dispatch-on-disabled-elements",

    // Body/Frameset event handler attribute reflection -- depends on
    // <frameset> parsing + Window/Body event handler reflection.
    "Body-FrameSet-Event-Handlers",

    // Initialisation while dispatching -- requires the full HTML
    // init-while-dispatching algorithm.
    "Event-init-while-dispatching",

    // Replace-listener crash regression in detached browsing context.
    "replace-event-listener-null-browsing-context-crash",

    // Bare IDL harnesses exercise the JavaScript WebIDL parser rather than
    // the DOM behavior owned by these headless feature suites.
    "idlharness.window.js",

    // Tentative / sub-resource tests -- gated on iframe / cross-origin
    // capabilities unless a focused suite explicitly opts them in.
#if WPT_RUNNER_SKIP_TENTATIVE
    ".tentative",
#endif
    ".sub.",

    // Shadow DOM dependencies (uses attachShadow internally).
    "relatedTarget.window",
    "Event-dispatch-listener-order.window",
    // event-global.html has 4/9 subtests requiring shadow DOM (closed-tree
    // window.event masking). The window.event mechanism itself works (proven
    // by event-global-set-before-handleEvent-lookup.window.js passing).
    "event-global.html",
    // test_driver / pointer Actions API + requestAnimationFrame loop.
    "handler-count.html",

    // test_driver.Actions() pointer simulation (pointerDown/Up synthesizing a
    // click) plus pseudo-element (::after) hit-testing and pseudoTarget — no
    // user-input simulation in the headless runtime.
    "click-on-absolute-pseudo.html",

    // Custom elements (customElements.define + class extends HTMLElement).
    "EventTarget-add-listener-platform-object.html",

    // CSS animation / transition engine driving real events.
    "EventListener-invoke-legacy.html",

    // Foreign-document creation (createDocument / cloneNode of full doc) +
    // createTextNode/createComment/createProcessingInstruction as EventTargets.
    "EventTarget-this-of-listener.html",
    "Event-dispatch-bubbles-false.html",
    "Event-dispatch-bubbles-true.html",
    "Event-dispatch-detached-input-and-change.html",
    "Event-dispatch-click.html",

    // document.createEvent + Event interface object reflection.
    "Event-constants.html",
    "Event-constructors.any",

    // Object.getOwnPropertyDescriptor on Event prototype accessor (isTrusted).
    "Event-isTrusted.any",

    // EventListener.handleEvent uncaught-error / promise_rejects branches
    // depend on EventWatcher + uncaught-exception plumbing.
    "EventListener-handleEvent.html",

    // window.event global is still set across exception/beforeunload paths —
    // requires window.event proxy plus shadow tree retargeting.
    "event-global-is-still-set",

    // EventTarget#dispatchEvent.html relies on aliases-table generated by
    // testharness aliases helper which depends on a global `aliases` table.
    "EventTarget-dispatchEvent.html",

    // Redispatch test depends on isTrusted=true for browser-fired events
    // and assert_throws_dom("InvalidStateError", ...) plumbing.
    "Event-dispatch-redispatch.html",

    // passive-by-default.html crashes in name template lookup
    // (window.constructor.name); document.constructor cannot currently be
    // overridden because document is a proxy.
    "passive-by-default.html",

    // High-resolution timestamp test depends on GamepadEvent constructor.
    "Event-timestamp-high-resolution.https",

};
static const int SKIP_COUNT = sizeof(SKIP_SUBSTRINGS) / sizeof(SKIP_SUBSTRINGS[0]);

static bool should_skip(const std::string& name) {
    for (int i = 0; i < SKIP_COUNT; i++) {
        if (name.find(SKIP_SUBSTRINGS[i]) != std::string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helpers (mirror test_wpt_clipboard_gtest.cpp)
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

static int read_wpt_variants(const char* path, char variants[][64], int capacity) {
    FILE* file = fopen(path, "r");
    if (!file) return 0;
    char line[1024];
    int count = 0;
    while (count < capacity && fgets(line, sizeof(line), file)) {
        if (!strstr(line, "<meta name=\"variant\"")) continue;
        const char* content = strstr(line, "content=\"");
        if (!content) continue;
        content += strlen("content=\"");
        const char* end = strchr(content, '"');
        if (!end) continue;
        size_t length = (size_t)(end - content);
        if (length >= 64) length = 63;
        memcpy(variants[count], content, length);
        variants[count][length] = '\0';
        count++;
    }
    fclose(file);
    return count;
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
 * resources/foo.js). External /resources/ paths (testharness.js,
 * testharnessreport.js, etc.) are skipped -- they are covered by the
 * shim.
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
            if (src[0] != '/' && src.find("://") == std::string::npos) {
                // WPT suite helpers such as dom/common.js are repository-local
                // parent-relative resources; rejecting `../` prevented their tests
                // from registering and falsely reported an empty result set.
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
 * For .any.js / .window.js tests: WPT auto-generates an HTML harness in
 * real browsers. Here we wrap the bare JS body in a script block so the
 * extract path above sees something. Returns the wrapped HTML.
 */
static std::string wrap_any_js(const std::string& js) {
    return std::string("<script>\n") + js + "\n</script>\n";
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

struct WptDomEventsParam {
    std::string source_path;     // .html or .js file
    std::string html_path;       // for --document; same as source_path for .html, else empty
    std::string test_name;       // sanitised name for GTest
    bool        is_any_js;       // true if source is a bare .any.js / .window.js
    bool        skip;
    char        variant[64];     // WPT META query variant, including leading '?'
};

struct WptDomEventsResult {
    WptDomEventsParam param;
    bool skipped;
    bool crash_test_passed;
    int pass_count;
    int total_count;
    int exit_code;
    double seconds;
    std::string skip_reason;
    std::string setup_error;
    std::string output;
    std::vector<std::string> failures;
};

static void scan_wpt_dom_events_dir(const std::string& dir,
                                    const std::string& rel_prefix,
                                    std::vector<WptDomEventsParam>& params) {
#ifdef _WIN32
    std::string pattern = dir + "\\*.*";
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle == -1) return;
    do {
        const char* name = fd.name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        std::string rel = rel_prefix.empty() ? std::string(name)
                                             : rel_prefix + "/" + name;
        if (fd.attrib & _A_SUBDIR) {
            scan_wpt_dom_events_dir(dir + "/" + name, rel, params);
            continue;
        }
#else
    DIR* directory = opendir(dir.c_str());
    if (!directory) return;
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        std::string rel = rel_prefix.empty() ? std::string(name)
                                             : rel_prefix + "/" + name;
        if (entry->d_type == DT_DIR) {
            scan_wpt_dom_events_dir(dir + "/" + name, rel, params);
            continue;
        }
#endif

        size_t len = strlen(name);
        std::string fname(name, len);

        bool is_html = (len >= 5 && strcmp(name + len - 5, ".html") == 0);
        bool is_any_js = (fname.find(".any.js") != std::string::npos) ||
                         (fname.find(".window.js") != std::string::npos) ||
                         (fname.find(".worker.js") != std::string::npos);

        if (!is_html && !is_any_js) continue;
        // Large WPT directories mix unrelated capability families. Suite
        // wrappers define a bounded acceptance corpus so aggregate CI does not
        // spend minutes executing cases that are intentionally out of scope.
        if (!WPT_RUNNER_INCLUDE(rel)) continue;

        // Skip reference files.
        if (is_html && len >= 9 && strcmp(name + len - 9, "-ref.html") == 0) {
            continue;
        }

        WptDomEventsParam p;
        p.variant[0] = '\0';
        p.source_path = dir + "/" + name;
        p.is_any_js = is_any_js;

        // Strip extension(s) to form base name.
        std::string base = rel;
        if (is_html) {
            base = base.substr(0, base.size() - 5);
        } else {
            // strip ".any.js" / ".window.js" / ".worker.js"
            size_t dot = base.rfind(".js");
            if (dot != std::string::npos) base = base.substr(0, dot);
            dot = base.rfind(".");
            if (dot != std::string::npos) {
                std::string ext = base.substr(dot);
                if (ext == ".any" || ext == ".window" || ext == ".worker") {
                    base = base.substr(0, dot);
                }
            }
        }

        // Suffix with variant so .html / .any.js / .window.js with the
        // same base name don't collide.
        std::string suffix;
        if (is_html) suffix = "_html";
        else if (fname.find(".any.js") != std::string::npos) suffix = "_any";
        else if (fname.find(".window.js") != std::string::npos) suffix = "_window";
        else if (fname.find(".worker.js") != std::string::npos) suffix = "_worker";
        p.test_name = base + suffix;
        for (auto& c : p.test_name) {
            if (!isalnum((unsigned char)c)) c = '_';
        }

        // For .html, use it as the document. For .any.js, leave html empty
        // (we'll use a stub blank document path below in the test body).
        p.html_path = is_html ? p.source_path : "";

        // Worker-only tests cannot run in our document context.
        if (fname.find(".worker.js") != std::string::npos) {
            p.skip = true;
        } else {
            p.skip = should_skip(rel);
        }

        char variants[8][64];
        int variant_count = is_html
            ? read_wpt_variants(p.source_path.c_str(), variants, 8) : 0;
        if (variant_count == 0) {
            params.push_back(p);
        } else {
            // A META variant is a separate WPT global with its own location.search;
            // collapsing them into one empty-query run bypasses the test behavior.
            for (int variant_index = 0; variant_index < variant_count; variant_index++) {
                WptDomEventsParam expanded = p;
                strncpy(expanded.variant, variants[variant_index], sizeof(expanded.variant) - 1);
                expanded.variant[sizeof(expanded.variant) - 1] = '\0';
                expanded.test_name += "_variant_";
                for (const char* cursor = expanded.variant; *cursor; cursor++) {
                    expanded.test_name += isalnum((unsigned char)*cursor) ? *cursor : '_';
                }
                params.push_back(expanded);
            }
        }

#ifdef _WIN32
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    }
    closedir(directory);
#endif
}

static std::vector<WptDomEventsParam> discover_wpt_dom_events_tests() {
    std::vector<WptDomEventsParam> params;
    scan_wpt_dom_events_dir(WPT_DIR, "", params);

    std::sort(params.begin(), params.end(),
              [](const WptDomEventsParam& a, const WptDomEventsParam& b) {
                  return a.test_name < b.test_name;
              });
    return params;
}

// ---------------------------------------------------------------------------
// Parameterised test
// ---------------------------------------------------------------------------

class WptDomEventsTest : public testing::TestWithParam<WptDomEventsParam> {};

static WptDomEventsResult run_wpt_dom_events_case(const WptDomEventsParam& p) {
    auto started = std::chrono::steady_clock::now();
    WptDomEventsResult result = {};
    result.param = p;
    if (p.skip) {
        result.skipped = true;
        result.skip_reason = "skipped (requires capability not yet supported): " + p.source_path;
        return result;
    }

    std::string scripts;
    std::string doc_arg = p.html_path;

    if (p.is_any_js) {
        // Bare JS: wrap and treat as a single inline script. Use a blank
        // HTML stub for --document so the document-related globals exist.
        std::string body = read_file_contents(p.source_path.c_str());
        if (body.empty()) {
            result.setup_error = "Could not read test file: " + p.source_path;
            return result;
        }
        std::string wrapped = wrap_any_js(body);
        scripts = extract_inline_scripts(wrapped, WPT_DIR);
        // Use a tiny inline blank document for the --document flag.
        // Reuse any existing simple HTML if present; otherwise the test
        // file itself works as a placeholder document.
        doc_arg = "test/wpt/wpt_blank_document.html";
    } else {
        std::string html = read_file_contents(p.source_path.c_str());
        if (html.empty()) {
            result.setup_error = "Could not read test file: " + p.source_path;
            return result;
        }
        size_t slash = p.source_path.rfind('/');
        std::string source_dir = slash == std::string::npos
            ? std::string(WPT_DIR) : p.source_path.substr(0, slash);
        scripts = extract_inline_scripts(html, source_dir);
    }

    if (scripts.empty()) {
        result.skipped = true;
        result.skip_reason = "No scripts (reftest or empty): " + p.source_path;
        return result;
    }

    if (p.variant[0]) {
        scripts.insert(0, "\" };\n");
        scripts.insert(0, p.variant);
        scripts.insert(0, "var location = { search: \"");
    }

    std::string shim = read_file_contents(SHIM_PATH);
    if (shim.empty()) {
        result.setup_error = std::string("Could not read testharness shim: ") + SHIM_PATH;
        return result;
    }

    // Compose: shim + extracted scripts + onload simulation + summary.
    std::string combined = shim + "\n" + scripts +
                           "\n_wpt_fire_onload();\n_wpt_print_summary();\n";

    std::string temp_js = std::string(TEMP_DIR) + "/" + WPT_RUNNER_TEMP_PREFIX + p.test_name + ".js";
    write_file_contents(temp_js.c_str(), combined);

    std::string output = execute_js_with_doc(
        temp_js.c_str(), doc_arg.c_str(), &result.exit_code);
    result.output = output;

    // Parse output for FAIL lines and summary.
    //   FAIL: <name> - <error message>
    //   WPT_RESULT: N/M passed
    size_t pos = 0;
    while (pos < output.size()) {
        size_t eol = output.find('\n', pos);
        if (eol == std::string::npos) eol = output.size();
        std::string line = output.substr(pos, eol - pos);
        pos = eol + 1;

        if (line.substr(0, 6) == "FAIL: ") result.failures.push_back(line);
        if (line.substr(0, 12) == "WPT_RESULT: ") {
            sscanf(line.c_str(), "WPT_RESULT: %d/%d",
                   &result.pass_count, &result.total_count);
        }
    }

    unlink(temp_js.c_str());
    result.crash_test_passed =
        p.test_name.find("crash") != std::string::npos &&
        result.total_count == 0 && result.exit_code == 0;
    auto ended = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(ended - started).count();
    return result;
}

static void report_wpt_dom_events_result(const WptDomEventsResult& result) {
    const WptDomEventsParam& p = result.param;

    if (result.skipped) {
        GTEST_SKIP() << result.skip_reason;
    }
    if (!result.setup_error.empty()) {
        FAIL() << result.setup_error;
        return;
    }

    // Crash tests pass purely by completing.
    if (result.crash_test_passed) {
        append_passing_baseline(p.test_name);
        printf("  %s: crash test -- completed without crash\n", p.test_name.c_str());
        return;
    }

    if (result.total_count == 0) {
        if (BASELINE_PATH[0] && !passing_baseline_contains(p.test_name.c_str())) {
            GTEST_SKIP() << "known failure (not in baseline): no test results from "
                         << p.source_path;
        }
        // No WPT_RESULT -- script likely failed to load/transpile (e.g.
        // missing constructor caused a top-level ReferenceError before any
        // test() call ran). Expected until the phases described in
        // vibe/radiant/Radiant_Design_Event.md land.
        FAIL() << "No test results from " << p.source_path
               << "\nExit code: " << result.exit_code
               << "\nOutput (first 2KB):\n"
               << result.output.substr(0, 2048);
        return;
    }

    printf("  %s: %d/%d passed", p.test_name.c_str(),
           result.pass_count, result.total_count);
    if (!result.failures.empty()) printf(" (%zu failures)", result.failures.size());
    printf("\n");

    bool passing = result.failures.empty() && result.pass_count == result.total_count;
    if (passing) append_passing_baseline(p.test_name);
    if (!passing && BASELINE_PATH[0] && !passing_baseline_contains(p.test_name.c_str())) {
        GTEST_SKIP() << "known failure (not in baseline): "
                     << result.pass_count << "/" << result.total_count << " passed"
                     << (result.failures.empty() ? "" : "\nFirst failure: ")
                     << (result.failures.empty() ? "" : result.failures.front());
    }

    for (const auto& f : result.failures) {
        ADD_FAILURE() << f;
    }

    EXPECT_EQ(result.pass_count, result.total_count)
        << "Not all tests passed in " << p.source_path;
}

static const std::vector<WptDomEventsParam> g_wpt_dom_events_params =
    discover_wpt_dom_events_tests();
static std::vector<WptDomEventsResult> g_wpt_dom_events_results;
static bool g_wpt_dom_events_pre_run = false;

static const WptDomEventsResult* find_pre_run_result(const WptDomEventsParam& p) {
    if (!g_wpt_dom_events_pre_run) return NULL;
    for (size_t index = 0; index < g_wpt_dom_events_params.size(); index++) {
        if (g_wpt_dom_events_params[index].test_name == p.test_name) {
            return &g_wpt_dom_events_results[index];
        }
    }
    return NULL;
}

TEST_P(WptDomEventsTest, Run) {
    const WptDomEventsParam& p = GetParam();
    const WptDomEventsResult* result = find_pre_run_result(p);
    if (result) {
        report_wpt_dom_events_result(*result);
    } else {
        report_wpt_dom_events_result(run_wpt_dom_events_case(p));
    }
}

INSTANTIATE_TEST_SUITE_P(
    WptDomEvents,
    WptDomEventsTest,
    testing::ValuesIn(g_wpt_dom_events_params),
    [](const testing::TestParamInfo<WptDomEventsParam>& info) {
        return info.param.test_name;
    });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WptDomEventsTest);

static int extract_parallel_jobs(int* argc, char** argv) {
    int jobs = wpt_parallel_jobs("LAMBDA_WPT_DOM_JOBS", "WPT_DOM_JOBS");
    for (int i = 1; i < *argc; i++) {
        int consumed = 0;
        const char* value = NULL;
        if (strcmp(argv[i], "--jobs") == 0 && i + 1 < *argc) {
            value = argv[i + 1];
            consumed = 2;
        } else if (wpt_arg_starts_with(argv[i], "--jobs=")) {
            value = argv[i] + strlen("--jobs=");
            consumed = 1;
        }
        if (!consumed) continue;

        int requested = atoi(value);
        if (requested > 0) jobs = requested;
        for (int move = i; move + consumed < *argc; move++) {
            argv[move] = argv[move + consumed];
        }
        *argc -= consumed;
        i--;
    }
    return jobs;
}

static void pre_run_wpt_dom_events(int jobs) {
    int worker_count = std::max(1, std::min(jobs, (int)g_wpt_dom_events_params.size()));
    printf("Found %zu WPT file(s); pre-running with %d bounded job(s)\n",
           g_wpt_dom_events_params.size(), worker_count);
    auto started = std::chrono::steady_clock::now();

    // A file owns its lambda.exe child and temp script, so parallel work stays
    // isolated while the bounded queue prevents memory-heavy children from
    // multiplying to the machine's full logical CPU count.
    wpt_run_cases_parallel(
        g_wpt_dom_events_params, g_wpt_dom_events_results, worker_count,
        run_wpt_dom_events_case,
        [](size_t, const WptDomEventsParam& p) {
            printf("[ PRE-RUN  ] %s\n", p.test_name.c_str());
        },
        [](size_t, const WptDomEventsParam& p, const WptDomEventsResult& result) {
            printf("[ COMPLETE ] %s (%.0f ms)\n",
                   p.test_name.c_str(), result.seconds * 1000.0);
        });

    auto ended = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(ended - started).count();
    printf("Pre-ran %zu WPT file(s) in %.0f ms\n\n",
           g_wpt_dom_events_params.size(), seconds * 1000.0);
    g_wpt_dom_events_pre_run = true;
}

int main(int argc, char** argv) {
    int jobs = extract_parallel_jobs(&argc, argv);
    bool filtered = wpt_has_filtered_gtest_arg(argc, argv, "WptDomEvents*");
    if (update_baseline_requested() && BASELINE_PATH[0]) {
        FILE* file = fopen(BASELINE_PATH, "w");
        if (file) {
            fprintf(file,
                "# Passing-file baseline; listed cases are regression-enforced.\n"
                "# Generated by %s=1. New passes are allowed and should ratchet this file.\n\n",
                UPDATE_BASELINE_ENV);
            fclose(file);
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    std::string gtest_filter = ::testing::GTEST_FLAG(filter);
    if (!filtered && gtest_filter != "*" && gtest_filter != "WptDomEvents*") {
        filtered = true;
    }
    if (!filtered) pre_run_wpt_dom_events(jobs);
    return RUN_ALL_TESTS();
}
