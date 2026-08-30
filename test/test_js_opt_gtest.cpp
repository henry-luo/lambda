// Focused JavaScript optimization-contract tests.
//
// These tests run one small JS fixture per child process with JS_OPT_TRACE=1
// and assert the selected optimization path in addition to the semantic
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
    "tla_drain",
    "uri_error_cache_hit",
    "uri_error_cache_miss",
    "named_fast_probe",
    "named_fast_hit",
    "named_fast_miss"
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
    "named_fast_no_key",
    "named_fast_host_dynamic",
    "named_fast_no_receiver",
    "named_fast_no_entry",
    "named_fast_attributes",
    "named_fast_bounds",
    "named_fast_reserved",
    "named_fast_deleted",
    "named_fast_value_type"
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
            size_t line_start = start;
            while (line_start > 0 && input[line_start - 1] != '\n') line_start--;
            const char* line_end = strchr(input + line_start, '\n');
            const char* stack_guard = strstr(input + line_start,
                "lambda_stack_overflow_error,");
            bool is_stack_guard_pointer = stack_guard &&
                (!line_end || stack_guard < line_end);
            // MIR pointer operands have host-dependent decimal widths. Normalize
            // the known stack-guard pointer and legacy ten-digit form while
            // retaining language constants (including tagged 64-bit literals).
            if (is_stack_guard_pointer || (digits == 10 && value >= 4300000000ULL)) {
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

// Run against the ordinary debug lambda.exe, which defines
// LAMBDA_JS_EXEC_PROFILE and which `make build` keeps current.
//
// This deliberately no longer prefers ./lambda-debug-profile.exe. Nothing in
// the normal build flow refreshes that artifact, and selecting it on mere
// existence bound the whole suite to whatever stale copy happened to be on
// disk. A copy built without LAMBDA_JS_EXEC_PROFILE compiles the trace hooks
// down to no-op inlines, so the child exits 0 and writes its MIR dump but
// never writes a .trace — turning every fixture here red with no hint why.
static const char* opt_executable() {
    const char* configured = getenv("LAMBDA_JS_OPT_EXE");
    if (configured && configured[0]) return configured;
#ifdef _WIN32
    return "lambda.exe";
#else
    return "./lambda.exe";
#endif
}

static bool run_fixture_mode(const char* name, const char* source, bool trace_enabled,
                             TraceResult* trace, char* output, size_t output_size) {
    ensure_opt_dir();
    char script_path[512];
    char trace_path[512];
    char mir_path[512];
    snprintf(script_path, sizeof(script_path), "%s/%s.js", kOptDir, name);
    snprintf(trace_path, sizeof(trace_path), "%s/%s%s.trace", kOptDir, name,
        trace_enabled ? "" : "_off");
    snprintf(mir_path, sizeof(mir_path), "%s/%s%s.mir", kOptDir, name,
        trace_enabled ? "" : "_off");
    remove(trace_path);
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
        {"JS_OPT_TRACE", trace_enabled ? "1" : "0"},
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
    int exit_code = result.exit_code;
    bool timed_out = result.timed_out;
    if (output && output_size > 0) {
        if (result.stdout_buf) {
            snprintf(output, output_size, "%s", result.stdout_buf);
        } else {
            output[0] = '\0';
        }
    }
    shell_result_free(&result);
    // Report which of these failure modes fired: they are otherwise
    // indistinguishable at the call site, which is what made a stale
    // non-profiling executable look like 19 unrelated contract failures.
    if (!ok) {
        fprintf(stderr, "js-opt fixture '%s': child %s exited %d%s\n",
            name, executable, exit_code, timed_out ? " (timed out)" : "");
        return false;
    }
    if (!trace_enabled) return OPT_ACCESS(trace_path, 0) != 0;

    char* trace_text = read_text(trace_path);
    if (!trace_text) {
        // A clean exit with no trace means the trace hooks compiled away:
        // js_opt_trace_dump() is a no-op inline unless LAMBDA_JS_EXEC_PROFILE
        // is defined for the build under test.
        fprintf(stderr,
            "js-opt fixture '%s': %s ran cleanly but wrote no trace to '%s'.\n"
            "  That binary was built without LAMBDA_JS_EXEC_PROFILE.\n"
            "  Rebuild it with `make build`, or set LAMBDA_JS_OPT_EXE to a"
            " profiling build.\n",
            name, executable, trace_path);
        return false;
    }
    bool parsed = parse_trace(trace_text, trace);
    free(trace_text);
    if (!parsed) {
        fprintf(stderr, "js-opt fixture '%s': malformed trace at '%s'\n",
            name, trace_path);
    }
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

static char* read_fixture_mir(const char* name) {
    char mir_path[512];
    snprintf(mir_path, sizeof(mir_path), "%s/%s.mir", kOptDir, name);
    return read_text(mir_path);
}

static const char* find_last_before(const char* begin, const char* end,
        const char* pattern) {
    if (!begin || !end || !pattern || begin >= end) return NULL;
    const char* last = NULL;
    const char* cursor = begin;
    while ((cursor = strstr(cursor, pattern)) && cursor < end) {
        last = cursor;
        cursor++;
    }
    return last;
}

static bool mir_branch_join_uses_merged_carrier(const char* mir,
        const char* function_prefix, const char* branch_anchor,
        const char* branch_opcode) {
    const char* function = mir && function_prefix
        ? strstr(mir, function_prefix) : NULL;
    const char* function_end = function ? strstr(function, "\n\tendfunc") : NULL;
    const char* anchor = function ? strstr(function, branch_anchor) : NULL;
    char branch_pattern[16];
    snprintf(branch_pattern, sizeof(branch_pattern), "\n\t%s\tL", branch_opcode);
    const char* branch = anchor ? strstr(anchor, branch_pattern) : NULL;
    if (!function || !function_end || !anchor || !branch ||
            branch >= function_end) return false;

    const char* branch_label = branch + strlen(branch_pattern) - 1;
    const char* branch_label_end = strchr(branch_label, ',');
    if (!branch_label_end || branch_label_end >= function_end) return false;
    char branch_marker[40];
    char branch_marker_crlf[40];
    int branch_label_len = (int)(branch_label_end - branch_label);
    if (branch_label_len <= 0 || branch_label_len + 3 >=
            (int)sizeof(branch_marker)) return false;
    if (branch_label_len + 4 >= (int)sizeof(branch_marker_crlf)) return false;
    branch_marker[0] = '\n';
    memcpy(branch_marker + 1, branch_label, (size_t)branch_label_len);
    branch_marker[branch_label_len + 1] = ':';
    branch_marker[branch_label_len + 2] = '\n';
    branch_marker[branch_label_len + 3] = '\0';
    // Windows MIR dumps use CRLF; accept both line endings when locating labels.
    branch_marker_crlf[0] = '\n';
    memcpy(branch_marker_crlf + 1, branch_label, (size_t)branch_label_len);
    branch_marker_crlf[branch_label_len + 1] = ':';
    branch_marker_crlf[branch_label_len + 2] = '\r';
    branch_marker_crlf[branch_label_len + 3] = '\n';
    branch_marker_crlf[branch_label_len + 4] = '\0';

    const char* branch_target = strstr(branch_label_end, branch_marker);
    if (!branch_target) branch_target = strstr(branch_label_end, branch_marker_crlf);
    if (!branch_target || branch_target >= function_end) return false;
    const char* join_jump = find_last_before(branch_label_end, branch_target,
        "\n\tjmp\tL");
    const char* merge_move = find_last_before(branch_label_end, join_jump,
        "\n\tmov\t%");
    if (!merge_move || !join_jump) return false;

    const char* destination = merge_move + strlen("\n\tmov\t");
    const char* destination_end = strchr(destination, ',');
    if (!destination_end || destination_end >= function_end) return false;
    char merged[32];
    int merged_len = (int)(destination_end - destination);
    if (merged_len <= 0 || merged_len >= (int)sizeof(merged)) return false;
    memcpy(merged, destination, (size_t)merged_len);
    merged[merged_len] = '\0';

    const char* label = join_jump + strlen("\n\tjmp\t");
    const char* label_end = strchr(label, '\n');
    if (!label_end || label_end >= function_end) return false;
    char join_marker[40];
    char join_marker_crlf[40];
    int label_len = (int)(label_end - label);
    if (label_len > 0 && label[label_len - 1] == '\r') label_len--;
    if (label_len <= 0 || label_len + 3 >= (int)sizeof(join_marker)) return false;
    if (label_len + 4 >= (int)sizeof(join_marker_crlf)) return false;
    join_marker[0] = '\n';
    memcpy(join_marker + 1, label, (size_t)label_len);
    join_marker[label_len + 1] = ':';
    join_marker[label_len + 2] = '\n';
    join_marker[label_len + 3] = '\0';
    join_marker_crlf[0] = '\n';
    memcpy(join_marker_crlf + 1, label, (size_t)label_len);
    join_marker_crlf[label_len + 1] = ':';
    join_marker_crlf[label_len + 2] = '\r';
    join_marker_crlf[label_len + 3] = '\n';
    join_marker_crlf[label_len + 4] = '\0';

    const char* join = strstr(branch_target, join_marker);
    if (!join) join = strstr(branch_target, join_marker_crlf);
    const char* error_test = join ? strstr(join, "\n\tursh\t") : NULL;
    if (!error_test || error_test >= function_end) return false;
    const char* first_comma = strchr(error_test, ',');
    if (!first_comma || first_comma >= function_end) return false;
    const char* carrier = first_comma + 1;
    while (*carrier == ' ' || *carrier == '\t') carrier++;
    size_t carrier_len = 0;
    while (carrier[carrier_len] && carrier[carrier_len] != ',' &&
            carrier[carrier_len] != '\n') carrier_len++;
    return carrier_len == strlen(merged) &&
        strncmp(carrier, merged, carrier_len) == 0;
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

TEST(JsOpt, RegexMediumCaptureFreeLoopReusesCompiledMatcher) {
    const char* source =
        "function makeRegex() { return /[A-Za-z0-9_\\u00A0-\\u00FF]/g; }\n"
        "var first = makeRegex(); var second = makeRegex();\n"
        "first.lastIndex = 4;\n"
        "if (first === second || second.lastIndex !== 0) throw new Error('shared object state');\n"
        "for (var i = 0; i < 32; i++) {\n"
        "  if (!makeRegex().test('Z')) throw new Error('bad matcher');\n"
        "}\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("regex_medium_capture_free_loop", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_REGEX_PERMANENT_CACHE_HIT][1], 16u);
    expect_trace_off_same("regex_medium_capture_free_loop", source, output);
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
        // A tagged element array reaches the fused Set kernel; an empty
        // numeric array is intentionally handled by its separate storage lane.
        "var a = [1, 'seed']; a[0] = 2; a[1] = 3;\n"
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
        "var a = [1, 'seed']; Object.preventExtensions(a); a[2] = 1;\n"
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

TEST(JsOpt, Result29NumberIndexedLaneUsesSharedReferenceSemantics) {
    const char* source =
        "var array = [10, 20]; var numberKey = 1.0;\n"
        "var old = array[numberKey]; array[numberKey] = old + 5;\n"
        "if (old !== 20 || array[1] !== 25) throw new Error('bad indexed lane');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("result29_number_indexed_lane", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);

    char* mir = read_fixture_mir("result29_number_indexed_lane");
    ASSERT_NE(mir, nullptr);
    // D1.3: numeric keys use the same property read/write kernels as every
    // other indexed key, so reference semantics have one compiler path.
    EXPECT_NE(strstr(mir, "js_get_reference"), nullptr);
    EXPECT_NE(strstr(mir, "js_set"), nullptr);
    EXPECT_EQ(strstr(mir, "js_get_number_reference"), nullptr);
    EXPECT_EQ(strstr(mir, "js_set_number_assignment"), nullptr);
    free(mir);
    expect_trace_off_same("result29_number_indexed_lane", source, output);
}

TEST(JsOpt, Result29TypedArrayUsesSharedReferenceSemantics) {
    const char* source =
        "function indexedGuards() {\n"
        "  const typed = new Uint8Array(8); const exact = 1 | 0;\n"
        "  typed[exact] = 3; typed[2] = 5;\n"
        "  return typed[exact] + typed[2];\n"
        "}\n"
        "if (indexedGuards() !== 8) throw new Error('typed array lane changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("result29_typed_array_guard", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);

    char* mir = read_fixture_mir("result29_typed_array_guard");
    ASSERT_NE(mir, nullptr);
    // D1.3: typed-array elements share the generic property kernels; the
    // runtime, rather than a duplicated compiler lane, owns their semantics.
    EXPECT_NE(strstr(mir, "js_get_reference"), nullptr);
    EXPECT_NE(strstr(mir, "js_set"), nullptr);
    EXPECT_EQ(strstr(mir, "js_typed_array_matches_type"), nullptr);
    EXPECT_EQ(strstr(mir, "js_typed_array_set"), nullptr);
    free(mir);
    expect_trace_off_same("result29_typed_array_guard", source, output);
}

TEST(JsOpt, Result29TypedArrayGuardRejectsShadowedConstructor) {
    const char* source =
        "function shadowed(Uint8Array) {\n"
        "  var values = new Uint8Array(2); values[0] = 7; return values[0];\n"
        "}\n"
        "function FakeTypedArray(length) { return {0: 1, length: length}; }\n"
        "if (shadowed(FakeTypedArray) !== 7 || shadowed(globalThis.Uint8Array) !== 7)\n"
        "  throw new Error('shadowed constructor was specialized');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("result29_shadowed_typed_array", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    // D6.2.2v2: a builtin-looking identifier is not a capability fact when
    // the lexical binding can be shadowed; semantics must remain generic.
    expect_trace_off_same("result29_shadowed_typed_array", source, output);
}

TEST(JsOpt, Result29DenseFillPreservesHoles) {
    const char* source =
        "var values = new Array(20000); values.fill(3, 100, 19900);\n"
        "if (0 in values || !(100 in values) || 19999 in values ||\n"
        "    values[100] !== 3 || values[19899] !== 3) throw new Error('fill changed holes');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("result29_dense_fill", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    // D6.2.2v2: the bulk fill may reserve storage, but it cannot materialize
    // elements outside the selected interval or change hole observability.
    expect_trace_off_same("result29_dense_fill", source, output);
}

TEST(JsOpt, Result29ArrayConstructedPrototypeIsPreserved) {
    const char* source =
        "function NewTarget() {}\n"
        "NewTarget.prototype = {marker: 23};\n"
        "var reflected = Reflect.construct(Array, [3], NewTarget);\n"
        "if (Object.getPrototypeOf(reflected).marker !== 23 || reflected.length !== 3)\n"
        "  throw new Error('constructed prototype was lost');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("result29_array_constructed_prototype", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    // D6.2.2v2: skipping canonical prototype re-installation is valid only
    // when an explicit newTarget prototype remains observable and intact.
    expect_trace_off_same("result29_array_constructed_prototype", source, output);
}

TEST(JsOpt, Result29IndexedFallbacksPreserveReferenceSemantics) {
    const char* source =
        "'use strict';\n"
        "var accessorArray = []; accessorArray[1] = 3; var seen = 0;\n"
        "Object.defineProperties(accessorArray, {\n"
        "  '1': {set: function (value) { seen = value; }, enumerable: true, configurable: true}\n"
        "});\n"
        "accessorArray[1] = 11;\n"
        "var rejectingProxy = new Proxy({}, {set: function () { return false; }});\n"
        "var proxyError = ''; try { rejectingProxy[4] = 10; } catch (error) { proxyError = error.name; }\n"
        "var fractional = [10, 20]; fractional[1.5] = 7;\n"
        "if (seen !== 11 || accessorArray[1] !== undefined || proxyError !== 'TypeError' ||\n"
        "    fractional[1.5] !== 7 || fractional[1] !== 20) throw new Error('indexed fallback changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("result29_indexed_fallbacks", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_SET_GUARD_FAIL][2], 0u);
    // The current proxy fallback is classified by the guard event itself; it
    // does not emit the retired shape_changed reason.
    expect_trace_off_same("result29_indexed_fallbacks", source, output);
}

TEST(JsOpt, Result29SuperIndexedAssignmentKeepsNullBaseError) {
    const char* source =
        "'use strict';\n"
        "var count = 0;\n"
        "class NullSuperWrite {\n"
        "  static run() { super[0] = count += 1; }\n"
        "}\n"
        "Object.setPrototypeOf(NullSuperWrite, null);\n"
        "var errorName = ''; try { NullSuperWrite.run(); } catch (error) { errorName = error.name; }\n"
        "if (errorName !== 'TypeError' || count !== 1) throw new Error('super reference changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("result29_super_indexed_assignment", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);

    char* mir = read_fixture_mir("result29_super_indexed_assignment");
    ASSERT_NE(mir, nullptr);
    // D8.4.3: super's lexical base must reach PutValue before indexed
    // specialization; otherwise a null parent incorrectly becomes a write.
    EXPECT_NE(strstr(mir, "js_super_property_set"), nullptr);
    free(mir);
    expect_trace_off_same("result29_super_indexed_assignment", source, output);
}

TEST(JsOpt, NamedLoadStoreUsesTierBPath) {
    const char* source =
        "function f(o) { o.x = o.x + 1; return o.x; }\n"
        "var a = {x: 1}; var n = 0;\n"
        "for (var i = 0; i < 8; i++) n += f(a);\n"
        "console.log(n); console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("named_fast_path", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_PROBE][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_HIT][1], 0u);
    char* mir = read_fixture_mir("named_fast_path");
    ASSERT_NE(mir, nullptr);
    // NameId is the compiler identity and the shared runtime head owns the
    // receiver, descriptor, prototype, and strictness fallback semantics.
    EXPECT_NE(strstr(mir, "js_get_name_id"), nullptr);
    EXPECT_NE(strstr(mir, "js_set_name_id"), nullptr);
    free(mir);
    expect_trace_off_same("named_fast_path", source, output);
}

TEST(JsOpt, MirLogicalJoinPublishesMergedCarrier) {
    const char* source =
        "function logicalJoinCarrier(e) {\n"
        "  return (null == e._pf && (e._pf = {ready: true}), e._pf);\n"
        "}\n"
        "var state = {_pf: {ready: true}};\n"
        "if (!logicalJoinCarrier(state).ready) throw new Error('bad join');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("mir_logical_join_carrier", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);

    char mir_path[512];
    snprintf(mir_path, sizeof(mir_path), "%s/%s.mir", kOptDir,
        "mir_logical_join_carrier");
    char* mir = read_text(mir_path);
    ASSERT_NE(mir, nullptr);
    // D8.4.3: a branch-local RHS register is undefined on the short-circuit
    // edge; the post-join ERROR test must consume the merged expression value.
    EXPECT_TRUE(mir_branch_join_uses_merged_carrier(
        mir, "_js_logicalJoinCarrier_", "js_equal,", "bt"));
    free(mir);
    expect_trace_off_same("mir_logical_join_carrier", source, output);
}

TEST(JsOpt, MirConditionalJoinPublishesMergedCarrier) {
    const char* source =
        "function conditionalJoinCarrier(flag, state) {\n"
        "  return ((flag ? (state.x = {ready: true}) : state.x), state.x);\n"
        "}\n"
        "var state = {x: {ready: true}};\n"
        "if (!conditionalJoinCarrier(false, state).ready) throw new Error('bad join');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("mir_conditional_join_carrier", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);

    char mir_path[512];
    snprintf(mir_path, sizeof(mir_path), "%s/%s.mir", kOptDir,
        "mir_conditional_join_carrier");
    char* mir = read_text(mir_path);
    ASSERT_NE(mir, nullptr);
    // D8.4.3: both conditional arms must publish the merged destination before
    // the post-join ERROR test; neither arm-local helper register dominates it.
    EXPECT_TRUE(mir_branch_join_uses_merged_carrier(
        mir, "_js_conditionalJoinCarrier_", "js_is_truthy,", "bf"));
    free(mir);
    expect_trace_off_same("mir_conditional_join_carrier", source, output);
}

TEST(JsOpt, UriErrorCacheRegistersRootAndHits) {
    const char* source =
        "for (let i = 0; i < 256; i++) {\n"
        "  try { decodeURIComponent('%E0%00'); }\n"
        "  catch (error) { if (!(error instanceof URIError)) throw error; }\n"
        "}\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("uri_error_cache", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    // D5.3.3: hits are recorded only after the cache's exact Item range is a
    // registered GC root, so the contract covers the ownership precondition.
    EXPECT_GE(trace.events[JS_OPT_URI_ERROR_CACHE_MISS][1], 1u);
    EXPECT_GE(trace.events[JS_OPT_URI_ERROR_CACHE_HIT][1], 255u);
    expect_trace_off_same("uri_error_cache", source, output);
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
