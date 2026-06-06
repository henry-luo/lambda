// =============================================================================
// JS Transpile Timing Benchmark (Tune6, first patch — measurement only)
// =============================================================================
//
// Focused, repeatable benchmark for the JavaScript frontend + MIR phases,
// isolated from full Radiant layout runs. It drives `lambda.exe js <fixture>`
// with JS_TRANSPILE_TIMING=1, which makes the CLI print two diagnostic lines
// sourced from the existing runtime APIs:
//
//   JS_TRANSPILE_TIMING file=... bytes=... parse_ms=.. ast_ms=.. early_ms=..
//       imports_ms=.. mir_ms=.. link_ms=.. exec_ms=.. cleanup_ms=.. total_ms=..
//   JS_AST_COUNTERS file=... scope_lookups=.. scope_entries_scanned=.. scopes_walked=..
//
// Phase timing comes from js_mir_get_last_phase_timing(JsMirPhaseTiming).
// The scope counters test whether the linear-scan js_scope_lookup is the
// AST-build bottleneck (entries_scanned grows ~O(n^2) on large minified libs).
//
// Pass/fail policy (per proposal): correctness only, never timing thresholds.
// A fixture passes if it compiled through the MIR phase (mir_ms > 0). We do NOT
// assert on the child exit code: vendor libraries often throw at top-level
// execution without a DOM/window, which is irrelevant to compile-phase timing.
//
// This is the "compile+execute" path. A true compile-only mode (skipping
// top-level execution) is a follow-up that needs a dedicated entry point.
//
// Usage:
//   ./test/test_js_transpile_timing_gtest.exe
//   ./test/test_js_transpile_timing_gtest.exe \
//       --gtest_filter=JSTranspileTiming.Phases/dom_jquery_lib_js
//
// Run against a RELEASE build of lambda.exe for any performance claim; debug
// numbers are for relative phase shape only.
// =============================================================================

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #define popen _popen
    #define pclose _pclose
    #define setenv(name, val, ow) _putenv_s((name), (val))
#else
    #include <unistd.h>
#endif

namespace {

struct Fixture {
    const char* label;  // gtest-safe name
    const char* path;   // path relative to project root (cwd during `make test`)
};

// Curated corpus from test/js (mixed sizes; all confirmed present in-tree).
// Ordered smallest-first so failures surface on cheap fixtures first.
const std::vector<Fixture>& fixtures() {
    static const std::vector<Fixture> f = {
        {"underscore_lib_js",  "test/js/underscore_lib.js"},   // ~32 KB
        {"ramda_src_min_js",   "test/js/ramda_src_min.js"},    // ~53 KB
        {"lib_lodash_js",      "test/js/lib_lodash.js"},       // ~78 KB
        {"lib_ajv_js",         "test/js/lib_ajv.js"},          // ~125 KB
        {"lib_yup_js",         "test/js/lib_yup.js"},          // ~159 KB
        {"lib_acorn_js",       "test/js/lib_acorn.js"},        // ~235 KB
        {"dom_jquery_lib_js",  "test/js/dom_jquery_lib.js"},   // ~291 KB (headline)
    };
    return f;
}

// Run `lambda.exe js <path>` with JS_TRANSPILE_TIMING=1 and capture stdout+stderr.
std::string run_with_timing(const char* path) {
    setenv("JS_TRANSPILE_TIMING", "1", 1);

    char command[1024];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js \"%s\" --no-log 2>&1", path);
#else
    // generous timeout: debug execution of large libs can be slow; release is fast
    snprintf(command, sizeof(command),
             "timeout 180 ./lambda.exe js \"%s\" --no-log 2>&1", path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) return std::string();

    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) out += buf;
    pclose(pipe);
    return out;
}

// Extract a `key=NUMBER` value from a single diagnostic line. Returns false if
// the key is absent.
bool extract_double(const std::string& line, const char* key, double* out) {
    std::string k = key;
    k += '=';
    size_t pos = line.find(k);
    if (pos == std::string::npos) return false;
    *out = atof(line.c_str() + pos + k.size());
    return true;
}

bool extract_long(const std::string& line, const char* key, long* out) {
    std::string k = key;
    k += '=';
    size_t pos = line.find(k);
    if (pos == std::string::npos) return false;
    *out = atol(line.c_str() + pos + k.size());
    return true;
}

// Pull a full line beginning with `prefix` out of multi-line output.
std::string find_line(const std::string& out, const char* prefix) {
    size_t pos = out.find(prefix);
    if (pos == std::string::npos) return std::string();
    size_t end = out.find('\n', pos);
    return out.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

}  // namespace

class JSTranspileTiming : public ::testing::TestWithParam<Fixture> {};

TEST_P(JSTranspileTiming, Phases) {
    const Fixture& fx = GetParam();

    // Skip (not fail) if the fixture is missing, so the suite stays portable.
    if (access(fx.path, 0 /*F_OK*/) != 0) {
        GTEST_SKIP() << "fixture not found: " << fx.path;
    }

    std::string out = run_with_timing(fx.path);
    ASSERT_FALSE(out.empty()) << "no output from lambda.exe js " << fx.path;

    std::string timing = find_line(out, "JS_TRANSPILE_TIMING ");
    std::string counters = find_line(out, "JS_AST_COUNTERS ");

    ASSERT_FALSE(timing.empty())
        << "missing JS_TRANSPILE_TIMING line — transpile likely crashed.\n"
        << "----- captured output (tail) -----\n"
        << (out.size() > 2000 ? out.substr(out.size() - 2000) : out);

    double parse_ms = 0, ast_ms = 0, mir_ms = 0, link_ms = 0, exec_ms = 0, total_ms = 0;
    extract_double(timing, "parse_ms", &parse_ms);
    extract_double(timing, "ast_ms", &ast_ms);
    extract_double(timing, "mir_ms", &mir_ms);
    extract_double(timing, "link_ms", &link_ms);
    extract_double(timing, "exec_ms", &exec_ms);
    extract_double(timing, "total_ms", &total_ms);

    long lookups = 0, scanned = 0, walked = 0;
    if (!counters.empty()) {
        extract_long(counters, "scope_lookups", &lookups);
        extract_long(counters, "scope_entries_scanned", &scanned);
        extract_long(counters, "scopes_walked", &walked);
    }

    // Correctness gate: compile must have reached MIR generation.
    EXPECT_GT(mir_ms, 0.0)
        << "MIR phase did not run — compilation failed for " << fx.path;

    // Surface the numbers in the test log (and as gtest properties for CI tools).
    printf("%s\n", timing.c_str());
    if (!counters.empty()) printf("%s\n", counters.c_str());

    // Average scope-entries scanned per lookup is the O(n^2) tell: it should be
    // roughly constant if scopes were hashed, and grow with file size today.
    double avg_scan = lookups > 0 ? (double)scanned / (double)lookups : 0.0;
    printf("JS_AST_DERIVED file=%s avg_entries_per_lookup=%.2f "
           "compile_ms=%.3f (parse+ast+mir+link) ast_share=%.1f%%\n",
           fx.path, avg_scan,
           parse_ms + ast_ms + mir_ms + link_ms,
           total_ms > 0 ? (100.0 * ast_ms / total_ms) : 0.0);

    RecordProperty("ast_ms", (int)ast_ms);
    RecordProperty("mir_ms", (int)mir_ms);
    RecordProperty("scope_entries_scanned", (int)scanned);
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, JSTranspileTiming, ::testing::ValuesIn(fixtures()),
    [](const ::testing::TestParamInfo<Fixture>& info) {
        return std::string(info.param.label);
    });
