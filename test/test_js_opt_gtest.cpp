// Focused JavaScript optimization-contract tests.
//
// These tests run one small JS fixture per child process with JS_OPT_TRACE=1
// and assert the selected cache/guard/IC path in addition to the semantic
// result. The profile-enabled child is intentional: release timing captures
// must remain free of contract tracing overhead.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define OPT_ACCESS _access
#define OPT_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define OPT_ACCESS access
#define OPT_MKDIR(path) mkdir(path, 0755)
#endif

extern "C" {
#include "../lib/shell.h"
}

#include "../lambda/js/js_exec_profile.h"

namespace {

static const char* kOptDir = "./temp/js_opt_contract";

static const char* kEventNames[JS_OPT_EVENT_COUNT] = {
    "scope_lookup_cache_hit",
    "scope_lookup_cache_miss",
    "fact_cache_hit",
    "fact_cache_miss",
    "load_ic_hit_mono",
    "load_ic_hit_poly",
    "load_ic_miss",
    "load_ic_install_mono",
    "load_ic_install_poly",
    "store_ic_hit_mono",
    "store_ic_hit_poly",
    "store_ic_miss",
    "store_ic_install_mono",
    "store_ic_install_poly",
    "regex_compile_cache_hit",
    "regex_compile_cache_miss",
    "regex_permanent_cache_hit",
    "regex_fresh_wrapper",
    "regex_keyless_reject",
    "regex_cache_invalidate",
    "array_set_fast_hit",
    "array_set_guard_fail",
    "dynamic_function_fastpath",
    "dynamic_function_cache_hit",
    "dynamic_function_cache_miss",
    "mir_direct_destination",
    "mir_discard_elision",
    "mir_branch_direct",
    "mir_generic_fallback",
    "mir_box_value",
    "mir_unbox_value",
    "mir_root_store",
    "module_cache_hit",
    "module_cache_miss",
    "tla_deferred_body",
    "tla_drain"
};

static const char* kReasonNames[JS_OPT_REASON_COUNT] = {
    "none",
    "hole_or_sparse",
    "prototype_accessor",
    "not_extensible",
    "length_not_writable",
    "capture_bearing_short_regex",
    "keyless_cache_entry",
    "shape_changed",
    "representation_mismatch",
    "tla_pending"
};

struct TraceResult {
    int schema;
    uint64_t events[JS_OPT_EVENT_COUNT][4];
    uint64_t reasons[JS_OPT_REASON_COUNT];
};

static void ensure_opt_dir() {
    if (OPT_ACCESS("./temp", 0) != 0) OPT_MKDIR("./temp");
    if (OPT_ACCESS(kOptDir, 0) != 0) OPT_MKDIR(kOptDir);
}

static bool write_text(const char* path, const char* text) {
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    size_t length = text ? strlen(text) : 0;
    bool ok = fwrite(text ? text : "", 1, length, file) == length;
    fclose(file);
    return ok;
}

static char* read_text(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    char* text = (char*)malloc((size_t)size + 1);
    if (!text) { fclose(file); return NULL; }
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size) { free(text); return NULL; }
    text[size] = '\0';
    return text;
}

static void normalize_child_output(const char* input, char* output, size_t capacity) {
    if (!output || capacity == 0) return;
    size_t out = 0;
    for (size_t i = 0; input && input[i] && out + 1 < capacity;) {
        // The normal log prefix contains wall-clock time. It is useful in a
        // failure, but not part of the JS semantic result being compared.
        if (i + 9 < strlen(input) && input[i] >= '0' && input[i] <= '9' &&
                input[i + 1] >= '0' && input[i + 1] <= '9' && input[i + 2] == ':' &&
                input[i + 3] >= '0' && input[i + 3] <= '9' &&
                input[i + 4] >= '0' && input[i + 4] <= '9' && input[i + 5] == ':' &&
                input[i + 6] >= '0' && input[i + 6] <= '9' &&
                input[i + 7] >= '0' && input[i + 7] <= '9' && input[i + 8] == ' ') {
            memcpy(output + out, "00:00:00 ", 9);
            out += 9;
            i += 9;
            continue;
        }
        output[out++] = input[i++];
    }
    output[out] = '\0';
}

static char* canonicalize_mir(const char* input) {
    if (!input) return NULL;
    size_t size = strlen(input);
    char* output = (char*)malloc(size * 2 + 1);
    if (!output) return NULL;
    size_t out = 0;
    for (size_t i = 0; i < size;) {
        if (input[i] >= '0' && input[i] <= '9') {
            size_t start = i;
            while (i < size && input[i] >= '0' && input[i] <= '9') i++;
            size_t digits = i - start;
            unsigned long long value = 0;
            for (size_t j = start; j < i; j++) {
                value = value * 10u + (unsigned long long)(input[j] - '0');
            }
            // MIR's linked runtime addresses are printed as ten decimal
            // digits. Canonicalize those process-local values while retaining
            // language constants (including tagged 64-bit numeric literals).
            if (digits == 10 && value >= 4300000000ULL) {
                const char* token = "<ptr>";
                memcpy(output + out, token, 5);
                out += 5;
            } else {
                memcpy(output + out, input + start, digits);
                out += digits;
            }
            continue;
        }
        output[out++] = input[i++];
    }
    output[out] = '\0';
    return output;
}

static int event_id(const char* name) {
    for (int i = 0; i < JS_OPT_EVENT_COUNT; i++) {
        if (strcmp(name, kEventNames[i]) == 0) return i;
    }
    return -1;
}

static int reason_id(const char* name) {
    for (int i = 0; i < JS_OPT_REASON_COUNT; i++) {
        if (strcmp(name, kReasonNames[i]) == 0) return i;
    }
    return -1;
}

static bool parse_u64(char** cursor, uint64_t* out) {
    if (!cursor || !*cursor || !out) return false;
    char* end = NULL;
    unsigned long long value = strtoull(*cursor, &end, 10);
    if (end == *cursor) return false;
    *cursor = end;
    *out = (uint64_t)value;
    return true;
}

static bool parse_trace(char* text, TraceResult* out) {
    if (!text || !out) return false;
    memset(out, 0, sizeof(*out));
    char* line = strstr(text, "JS_OPT_TRACE schema=");
    if (!line) return false;
    if (strstr(line + strlen("JS_OPT_TRACE schema="), "JS_OPT_TRACE schema=")) {
        return false;
    }
    char* schema = line + strlen("JS_OPT_TRACE schema=");
    char* schema_end = NULL;
    out->schema = (int)strtol(schema, &schema_end, 10);
    if (schema_end == schema || out->schema != 1) return false;

    char* events = strstr(schema_end, " events=");
    char* reasons = strstr(schema_end, " reasons=");
    if (!events || !reasons || reasons <= events) return false;
    events += strlen(" events=");
    char* reason_data = reasons + strlen(" reasons=");
    *reasons = '\0';
    reasons = reason_data;
    bool seen_events[JS_OPT_EVENT_COUNT] = {};
    bool seen_reasons[JS_OPT_REASON_COUNT] = {};

    char* token = strtok(events, ",\r\n");
    while (token) {
        char* equal = strchr(token, '=');
        if (!equal) return false;
        *equal = '\0';
        int id = event_id(token);
        if (id < 0 || seen_events[id]) return false;
        seen_events[id] = true;
        char* cursor = equal + 1;
        for (int i = 0; i < 4; i++) {
            if (!parse_u64(&cursor, &out->events[id][i])) return false;
            if (i != 3 && *cursor++ != '/') return false;
        }
        if (*cursor != '\0') return false;
        token = strtok(NULL, ",\r\n");
    }

    token = strtok(reasons, ",\r\n");
    while (token) {
        char* equal = strchr(token, '=');
        if (!equal) return false;
        *equal = '\0';
        int id = reason_id(token);
        if (id < 0 || seen_reasons[id]) return false;
        seen_reasons[id] = true;
        char* cursor = equal + 1;
        if (!parse_u64(&cursor, &out->reasons[id]) || *cursor != '\0') return false;
        token = strtok(NULL, ",\r\n");
    }
    for (int i = 0; i < JS_OPT_EVENT_COUNT; i++) if (!seen_events[i]) return false;
    for (int i = 0; i < JS_OPT_REASON_COUNT; i++) if (!seen_reasons[i]) return false;
    return true;
}

static const char* opt_executable() {
    const char* configured = getenv("LAMBDA_JS_OPT_EXE");
    if (configured && configured[0]) return configured;
#ifdef _WIN32
    if (OPT_ACCESS("lambda-debug-profile.exe", 0) == 0) return "lambda-debug-profile.exe";
    return "lambda.exe";
#else
    if (OPT_ACCESS("./lambda-debug-profile.exe", X_OK) == 0) return "./lambda-debug-profile.exe";
    return "./lambda.exe";
#endif
}

static bool run_fixture_mode(const char* name, const char* source, bool trace_enabled,
                             TraceResult* trace, char* output, size_t output_size) {
    ensure_opt_dir();
    char script_path[512];
    char trace_path[512];
    char profile_path[512];
    char mir_path[512];
    snprintf(script_path, sizeof(script_path), "%s/%s.js", kOptDir, name);
    snprintf(trace_path, sizeof(trace_path), "%s/%s%s.trace", kOptDir, name,
        trace_enabled ? "" : "_off");
    snprintf(profile_path, sizeof(profile_path), "%s/%s%s.profile", kOptDir, name,
        trace_enabled ? "" : "_off");
    snprintf(mir_path, sizeof(mir_path), "%s/%s%s.mir", kOptDir, name,
        trace_enabled ? "" : "_off");
    remove(trace_path);
    remove(profile_path);
    remove(mir_path);
    if (!write_text(script_path, source)) return false;

    const char* executable = opt_executable();
    // MIR dumping is intentionally gated by --no-log. Keep logging enabled for
    // this child so the finalized artifact is available for the differential
    // contract below; the child writes diagnostics to its normal log sink.
    const char* args[] = {executable, "js", script_path, NULL};
    ShellEnvEntry env[] = {
        // Keep the compilation profile mode identical in both runs. The
        // differential toggles only contract tracing; changing the profiler
        // mode would legitimately enable/disable unrelated MIR probes.
        {"JS_EXEC_PROFILE", "1"},
        {"JS_OPT_TRACE", trace_enabled ? "1" : "0"},
        {"JS_EXEC_PROFILE_OUT", profile_path},
        {"JS_OPT_TRACE_OUT", trace_path},
        {"LAMBDA_MIR_DUMP_PATH", mir_path},
        {NULL, NULL}
    };
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = 30000;
    options.merge_stderr = true;
    ShellResult result = shell_exec(executable, args, &options);
    bool ok = result.exit_code == 0 && !result.timed_out;
    if (output && output_size > 0) {
        if (result.stdout_buf) {
            snprintf(output, output_size, "%s", result.stdout_buf);
        } else {
            output[0] = '\0';
        }
    }
    shell_result_free(&result);
    if (!ok) return false;
    if (!trace_enabled) return OPT_ACCESS(trace_path, 0) != 0;

    char* trace_text = read_text(trace_path);
    if (!trace_text) return false;
    bool parsed = parse_trace(trace_text, trace);
    free(trace_text);
    return parsed;
}

static bool run_fixture(const char* name, const char* source, TraceResult* trace,
                        char* output, size_t output_size) {
    return run_fixture_mode(name, source, true, trace, output, output_size);
}

static void expect_trace_off_same(const char* name, const char* source,
                                  const char* trace_output) {
    char output[4096];
    ASSERT_TRUE(run_fixture_mode(name, source, false, NULL, output, sizeof(output)));
    char normalized_trace_output[4096];
    char normalized_output[4096];
    normalize_child_output(trace_output, normalized_trace_output,
        sizeof(normalized_trace_output));
    normalize_child_output(output, normalized_output, sizeof(normalized_output));
    EXPECT_STREQ(normalized_trace_output, normalized_output);

    char mir_path[512];
    char mir_off_path[512];
    snprintf(mir_path, sizeof(mir_path), "%s/%s.mir", kOptDir, name);
    snprintf(mir_off_path, sizeof(mir_off_path), "%s/%s_off.mir", kOptDir, name);
    char* mir = read_text(mir_path);
    char* mir_off = read_text(mir_off_path);
    ASSERT_NE(mir, nullptr) << "trace-on finalized MIR artifact is missing";
    ASSERT_NE(mir_off, nullptr) << "trace-off finalized MIR artifact is missing";
    char* canonical_mir = canonicalize_mir(mir);
    char* canonical_mir_off = canonicalize_mir(mir_off);
    ASSERT_NE(canonical_mir, nullptr);
    ASSERT_NE(canonical_mir_off, nullptr);
    EXPECT_STREQ(canonical_mir, canonical_mir_off);
    free(canonical_mir);
    free(canonical_mir_off);
    free(mir);
    free(mir_off);
}

static char* copy_text(const char* text) {
    if (!text) return NULL;
    size_t size = strlen(text);
    char* copy = (char*)malloc(size + 1);
    if (!copy) return NULL;
    memcpy(copy, text, size + 1);
    return copy;
}

static char* make_long_regex_source() {
    const int repetitions = 1200;
    const int capacity = repetitions * 2 + 256;
    char* source = (char*)malloc((size_t)capacity);
    if (!source) return NULL;
    int pos = snprintf(source, (size_t)capacity,
        "var a = /[");
    for (int i = 0; i < repetitions && pos + 8 < capacity; i++) {
        source[pos++] = (char)('a' + (i % 26));
    }
    pos += snprintf(source + pos, (size_t)(capacity - pos),
        "]+/; var b = /[");
    for (int i = 0; i < repetitions && pos + 8 < capacity; i++) {
        source[pos++] = (char)('a' + (i % 26));
    }
    pos += snprintf(source + pos, (size_t)(capacity - pos),
        "]+/; console.log(a.test('abc') && b.test('abc')); console.log('OPT_OK');\n");
    return source;
}

static void expect_ok_output(const char* output) {
    ASSERT_NE(output, nullptr);
    ASSERT_NE(strstr(output, "OPT_OK"), nullptr) << output;
}

}  // namespace

TEST(JsOpt, TraceParserFailsClosed) {
    const char* source = "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("trace_parser", source, &trace,
                            output, sizeof(output)));
    char trace_path[512];
    snprintf(trace_path, sizeof(trace_path), "%s/%s.trace", kOptDir,
        "trace_parser");
    char* valid = read_text(trace_path);
    ASSERT_NE(valid, nullptr);

    size_t size = strlen(valid);
    char* duplicate = (char*)malloc(size * 2 + 1);
    ASSERT_NE(duplicate, nullptr);
    memcpy(duplicate, valid, size);
    memcpy(duplicate + size, valid, size + 1);
    EXPECT_FALSE(parse_trace(duplicate, &trace));
    free(duplicate);

    char* unknown_schema = copy_text(valid);
    ASSERT_NE(unknown_schema, nullptr);
    char* schema = strstr(unknown_schema, "schema=1");
    ASSERT_NE(schema, nullptr);
    schema[strlen("schema=")] = '2';
    EXPECT_FALSE(parse_trace(unknown_schema, &trace));
    free(unknown_schema);

    char* unknown_event = copy_text(valid);
    ASSERT_NE(unknown_event, nullptr);
    char* event = strstr(unknown_event, "scope_lookup_cache_hit");
    ASSERT_NE(event, nullptr);
    event[0] = 'X';
    EXPECT_FALSE(parse_trace(unknown_event, &trace));
    free(unknown_event);

    char* truncated = copy_text(valid);
    ASSERT_NE(truncated, nullptr);
    char* reasons = strstr(truncated, " reasons=");
    ASSERT_NE(reasons, nullptr);
    *reasons = '\0';
    EXPECT_FALSE(parse_trace(truncated, &trace));
    free(truncated);
    free(valid);
}

TEST(JsOpt, RegexCompileCacheHit) {
    char* source = make_long_regex_source();
    ASSERT_NE(source, nullptr);
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("regex_compile_cache_hit", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_REGEX_COMPILE_CACHE_HIT][1], 0u);
    expect_trace_off_same("regex_compile_cache_hit", source, output);
    free(source);
}

TEST(JsOpt, RegexShortCaptureUsesFreshWrapper) {
    const char* source =
        "var a = /(a)/; var b = /(a)/;\n"
        "console.log(a.exec('a')[1] + b.exec('a')[1]);\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("regex_short_capture", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_REGEX_FRESH_WRAPPER][1], 0u);
    EXPECT_GT(trace.reasons[JS_OPT_REASON_CAPTURE_BEARING_SHORT_REGEX], 0u);
    EXPECT_EQ(trace.events[JS_OPT_REGEX_KEYLESS_REJECT][1], 0u);
    expect_trace_off_same("regex_short_capture", source, output);
}

TEST(JsOpt, DenseArrayStoreTakesFastPath) {
    const char* source =
        "var a = []; a[0] = 1; a[1] = 2;\n"
        "console.log(a[0] + a[1]); console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("array_dense_store", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_SET_FAST_HIT][1], 0u);
    expect_trace_off_same("array_dense_store", source, output);
}

TEST(JsOpt, NonExtensibleArrayFallsBack) {
    const char* source =
        "var a = []; Object.preventExtensions(a); a[0] = 1;\n"
        "console.log(a.length); console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("array_non_extensible", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_SET_GUARD_FAIL][2], 0u);
    EXPECT_GT(trace.reasons[JS_OPT_REASON_NOT_EXTENSIBLE], 0u);
    expect_trace_off_same("array_non_extensible", source, output);
}

TEST(JsOpt, NamedLoadStoreICWarms) {
    const char* source =
        "function f(o) { o.x = o.x + 1; return o.x; }\n"
        "var a = {x: 1}; var n = 0;\n"
        "for (var i = 0; i < 8; i++) n += f(a);\n"
        "console.log(n); console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("named_ic_warm", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_LOAD_IC_HIT_MONO][1] +
                  trace.events[JS_OPT_LOAD_IC_HIT_POLY][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STORE_IC_HIT_MONO][1] +
                  trace.events[JS_OPT_STORE_IC_HIT_POLY][1], 0u);
    expect_trace_off_same("named_ic_warm", source, output);
}

TEST(JsOpt, DynamicFunctionReturnIdentifierFastPath) {
    const char* source =
        "var x = 7; var f = new Function('return x');\n"
        "console.log(f(7)); console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("dynamic_function_fastpath", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_DYNAMIC_FUNCTION_FASTPATH][1], 0u);
    expect_trace_off_same("dynamic_function_fastpath", source, output);
}

TEST(JsOpt, DynamicFunctionCacheHit) {
    const char* source =
        "var f = new Function('return 7');\n"
        "var g = new Function('return 7');\n"
        "console.log(f() + g()); console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("dynamic_function_cache", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_DYNAMIC_FUNCTION_CACHE_HIT][1], 0u);
    expect_trace_off_same("dynamic_function_cache", source, output);
}
