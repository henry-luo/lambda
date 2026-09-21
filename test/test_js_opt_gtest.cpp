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
    "regex_bulk_match_hit",
    "regex_bulk_match_fallback",
    "regex_bulk_replace_hit",
    "regex_bulk_replace_fallback",
    "array_set_fast_hit",
    "array_set_guard_fail",
    "array_own_element_get_hit",
    "array_own_element_get_fallback",
    "array_reduce_dense_element",
    "array_reduce_prerooted_args",
    "array_runtime_items_install",
    "array_runtime_items_release",
    "array_gc_items_alloc",
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
    "named_fast_miss",
    "named_fast_string_length",
    "named_fast_data_descriptor",
    "named_fast_no_receiver_string",
    "named_fast_no_receiver_function",
    "named_fast_no_receiver_other",
    "named_fast_function_data",
    "runtime_number_head_hit",
    "runtime_number_head_fallback",
    "runtime_string_concat_head",
    "mir_number_admitted",
    "mir_number_fallback",
    "mir_native_index_admitted",
    "mir_native_index_fallback",
    "mir_dense_index_admitted",
    "mir_packed_strict_equal",
    "mir_loop_stable_name_id",
    "mir_light_call",
    "mir_light_direct_activation",
    "bound_call_forward_args",
    "mir_deferred_function_finalize",
    "mir_lazy_function_metadata",
    "ordinary_number_store",
    "typed_number_store",
    "typed_number_read",
    "own_enumerability_inspect",
    "mir_literal_field_admitted",
    "static_numeric_array_initializer",
    "static_object_initializer",
    "string_search_ascii",
    "string_search_unicode",
    "string_split_ascii",
    "string_split_unicode",
    "string_slice_ascii",
    "string_slice_unicode",
    "string_char_access_ascii",
    "string_char_access_unicode",
    "string_concat_ascii",
    "string_concat_unicode",
    "ascii_substring_cache_hit",
    "ascii_substring_cache_miss",
    "runtime_to_numeric_call",
    "runtime_increment_call",
    "runtime_decrement_call",
    "runtime_boxed_compare_call",
    "runtime_number_index_get_call",
    "runtime_number_index_numeric_array",
    "runtime_number_index_tagged_array",
    "runtime_number_index_tagged_packed_plain",
    "runtime_number_index_tagged_holey_plain",
    "runtime_number_index_tagged_with_props",
    "runtime_number_index_tagged_other",
    "runtime_number_index_typed_array",
    "runtime_number_index_other"
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
            // MIR pointer operands have host-dependent decimal widths. AArch64
            // static-recipe addresses may sit above 2^48; tagged Items start
            // at 2^56, so this still preserves language-value constants.
            bool is_user_pointer = value >= 4300000000ULL &&
                value < 72057594037927936ULL;
            if (is_stack_guard_pointer || is_user_pointer) {
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

// run against the executable supplied by the test target. `make test-js-opt`
// builds the ordinary debug host, which carries the compile-time hooks;
// callers may override that path for a different instrumented binary.
static const char* opt_executable() {
    const char* configured = getenv("LAMBDA_JS_OPT_EXE");
    if (configured && configured[0]) return configured;
#ifdef _WIN32
    return "lambda.exe";
#else
    return "./lambda.exe";
#endif
}

static bool run_fixture_mode_backend(const char* name, const char* source,
                                     bool trace_enabled, const char* backend,
                                     TraceResult* trace, char* output,
                                     size_t output_size, int timeout_ms = 30000) {
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
    ShellEnvEntry env[5] = {};
    int env_count = 0;
    if (backend) env[env_count++] = {"JS_EXECUTION_BACKEND", backend};
    // Keep the compilation profile mode identical in both runs. The
    // differential toggles only contract tracing; changing the profiler
    // mode would legitimately enable/disable unrelated MIR probes.
    env[env_count++] = {"JS_OPT_TRACE", trace_enabled ? "1" : "0"};
    env[env_count++] = {"JS_OPT_TRACE_OUT", trace_path};
    env[env_count++] = {"LAMBDA_MIR_DUMP_PATH", mir_path};
    ShellOptions options = {};
    options.env = env;
    options.timeout_ms = timeout_ms;
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
            "  Rebuild it with `make debug`, or set LAMBDA_JS_OPT_EXE to a"
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

static bool run_fixture_mode(const char* name, const char* source, bool trace_enabled,
                             TraceResult* trace, char* output, size_t output_size,
                             int timeout_ms = 30000) {
    return run_fixture_mode_backend(name, source, trace_enabled, NULL, trace,
        output, output_size, timeout_ms);
}

static bool run_fixture(const char* name, const char* source, TraceResult* trace,
                        char* output, size_t output_size) {
    return run_fixture_mode(name, source, true, trace, output, output_size);
}

static void expect_trace_off_same(const char* name, const char* source,
                                  const char* trace_output,
                                  const char* backend = NULL) {
    char output[4096];
    ASSERT_TRUE(run_fixture_mode_backend(name, source, false, backend, NULL,
        output, sizeof(output)));
    char normalized_trace_output[4096];
    char normalized_output[4096];
    normalize_child_output(trace_output, normalized_trace_output,
        sizeof(normalized_trace_output));
    normalize_child_output(output, normalized_output, sizeof(normalized_output));
    EXPECT_STREQ(normalized_trace_output, normalized_output);

    // The AST backend shares runtime helpers but has no finalized MIR artifact.
    if (backend && strcmp(backend, "ast") == 0) return;

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

static const char* find_mir_function(const char* mir, const char* marker,
        const char** end_out, const char* required_line_text = NULL) {
    if (end_out) *end_out = NULL;
    if (!mir || !marker) return NULL;
    const char* candidate = mir;
    while ((candidate = strstr(candidate, marker))) {
        const char* line = candidate;
        while (line > mir && line[-1] != '\n') line--;
        const char* line_end = strchr(line, '\n');
        const char* function = strstr(line, ":\tfunc\t");
        const char* required = required_line_text
            ? strstr(line, required_line_text) : line;
        if (function && (!line_end || function < line_end) &&
                required && (!line_end || required < line_end)) {
            const char* end = strstr(function, "\n\tendfunc");
            if (!end) return NULL;
            if (end_out) *end_out = end;
            return line;
        }
        candidate++;
    }
    return NULL;
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

static bool mir_branch_join_has_defined_carrier(const char* mir,
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
    if (error_test && error_test < function_end) {
        const char* first_comma = strchr(error_test, ',');
        if (!first_comma || first_comma >= function_end) return false;
        const char* carrier = first_comma + 1;
        while (*carrier == ' ' || *carrier == '\t') carrier++;
        size_t carrier_len = 0;
        while (carrier[carrier_len] && carrier[carrier_len] != ',' &&
                carrier[carrier_len] != '\n') carrier_len++;
        if (carrier_len == strlen(merged) &&
                strncmp(carrier, merged, carrier_len) == 0) {
            return true;
        }
    }

    // A branch may already have routed every fallible helper before its
    // normal edge. In that form D8.4.3 still requires the right arm to
    // define the same merged result rather than leaking an arm-local value.
    char right_move[40];
    int move_len = snprintf(right_move, sizeof(right_move), "\n\tmov\t%s,",
        merged);
    if (move_len <= 0 || move_len >= (int)sizeof(right_move)) return false;
    const char* right_publish = strstr(branch_target, right_move);
    return right_publish && right_publish < join;
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

TEST(JsOpt, RegexTestUsesCompiledPatternAfterPublicMetadataOverride) {
    const char* source =
        "var regex = /x/;\n"
        "Object.defineProperty(regex, 'source', { value: '^\\\\p{Script=Han}+$', configurable: true });\n"
        "Object.defineProperty(regex, 'flags', { value: 'u', configurable: true });\n"
        "if (!regex.test('x') || regex.test('漢'))\n"
        "  throw new Error('RegExp test read mutable public metadata');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("regexp_compiled_metadata", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    expect_trace_off_same("regexp_compiled_metadata", source, output);
}

TEST(JsOpt, BuiltinRegexBulkPathsKeepProtocolOverrides) {
    const char* source =
        "var words = 'one 22 two'.match(new RegExp('[a-z]+', 'g'));\n"
        "var replaced = 'one 22 two'.replace(new RegExp('[a-z]+', 'g'), '[$&]');\n"
        "function customExec(value) { return null; }\n"
        "var custom = new RegExp('a', 'g'); custom.exec = customExec;\n"
        "var customResult = 'a'.match(custom);\n"
        "var frozen = new RegExp('a', 'g');\n"
        "Object.defineProperty(frozen, 'lastIndex', { writable: false });\n"
        "var caught = false; try { 'a'.match(frozen); } catch (error) { caught = error.name === 'TypeError'; }\n"
        "if (words.join(',') !== 'one,two' || replaced !== '[one] 22 [two]' ||\n"
        "    customResult !== null || !caught) throw new Error('bulk regexp changed protocol');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture_mode_backend("regexp_bulk_protocol", source, true,
        "ast", &trace, output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_REGEX_BULK_MATCH_HIT][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_REGEX_BULK_REPLACE_HIT][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_REGEX_BULK_MATCH_FALLBACK][2], 1u);
    expect_trace_off_same("regexp_bulk_protocol", source, output, "ast");
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

TEST(JsOpt, RuntimeNumberHeadKeepsCoercingCasesOnSlowPath) {
    const char* source =
        "function operate(a, b, op) {\n"
        "  if (op === 0) return a + b;\n"
        "  if (op === 1) return a - b;\n"
        "  if (op === 2) return a * b;\n"
        "  return a / b;\n"
        "}\n"
        "var calls = 0;\n"
        "var coercing = { valueOf: function() { calls++; return 4; } };\n"
        "if (operate(1.5, 2.5, 0) !== 4 || operate(9, 2, 1) !== 7 ||\n"
        "    operate(3, 4, 2) !== 12 || operate(9, 2, 3) !== 4.5 ||\n"
        "    operate('x', 2, 0) !== 'x2' || operate(coercing, 1, 0) !== 5 ||\n"
        "    calls !== 1) throw new Error('runtime number head changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture_mode_backend("runtime_number_head", source, true,
        "ast", &trace, output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_NUMBER_HEAD_HIT][1], 3u);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_NUMBER_HEAD_FALLBACK][2], 1u);
    expect_trace_off_same("runtime_number_head", source, output, "ast");
}

TEST(JsOpt, RuntimeOwnDenseElementHeadsPreserveFallbackSemantics) {
    const char* source =
        "var values = [1, 'two']; Object.preventExtensions(values);\n"
        "values[0] = 3;\n"
        "var hole = new Array(1); Object.prototype[0] = 'prototype';\n"
        "var inherited = hole[0]; delete Object.prototype[0];\n"
        "if (values[0] !== 3 || inherited !== 'prototype') "
        "throw new Error('own dense element head changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture_mode_backend("runtime_own_dense_element", source,
        true, "ast", &trace, output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_OWN_ELEMENT_GET_HIT][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_OWN_ELEMENT_GET_FALLBACK][2], 0u);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_SET_FAST_HIT][1], 0u);
    expect_trace_off_same("runtime_own_dense_element", source, output, "ast");
}

TEST(JsOpt, DenseReduceRevalidatesAfterCallbackMutation) {
    const char* source =
        "var values = [1, 2, 3];\n"
        "var total = values.reduce(function(acc, value, index, source) {\n"
        "  if (index === 0) delete source[1];\n"
        "  return acc + value;\n"
        "}, 0);\n"
        "var getter_calls = 0; var accessor = [1, 2];\n"
        "Object.defineProperty(accessor, '1', { get: function() {\n"
        "  getter_calls++; return 4; }, configurable: true });\n"
        "var accessor_total = accessor.reduce(function(acc, value) {\n"
        "  return acc + value;\n"
        "}, 0);\n"
        "if (total !== 4 || accessor_total !== 5 || getter_calls !== 1) "
        "throw new Error('reduce dense leaf changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture_mode_backend("dense_reduce_revalidation", source,
        true, "ast", &trace, output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_REDUCE_DENSE_ELEMENT][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_REDUCE_PREROOTED_ARGS][1], 0u);
    expect_trace_off_same("dense_reduce_revalidation", source, output, "ast");
}

TEST(JsOpt, MirNumberPlanUsesF64AndKeepsPartialFactsBoxed) {
    const char* source =
        "function nativeNumberLoop() {\n"
        "  let total = 0; for (let i = 0; i < 16; i++) total = total + 0.5;\n"
        "  return total === 8;\n"
        "}\n"
        "function boolMutation() {\n"
        "  let value = true; value = value + 0.5; return value === 1.5;\n"
        "}\n"
        "function partialNumber(value) { return value + 1; }\n"
        "if (!nativeNumberLoop() || !boolMutation() || partialNumber('n') !== 'n1') "
        "throw new Error('number plan changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("mir_number_plan", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_MIR_NUMBER_ADMITTED][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_MIR_NUMBER_FALLBACK][2], 0u);

    char* mir = read_fixture_mir("mir_number_plan");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "dadd"), nullptr);
    EXPECT_NE(strstr(mir, "dlt"), nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_add"), nullptr);
    free(mir);
    expect_trace_off_same("mir_number_plan", source, output);
}

TEST(JsOpt, NativeNumberUpdatesKeepPostfixAndGenericSemantics) {
    const char* source =
        "function nativeUpdate(value) {\n"
        "  let before = value++;\n"
        "  let after = --value;\n"
        "  return before * 10 + after;\n"
        "}\n"
        "function nativePostfix(value) { return value++; }\n"
        "function dynamicUpdate(value) { return value++; }\n"
        "if (nativeUpdate(2.5) !== 27.5 || !Object.is(nativePostfix(-0), -0) ||\n"
        "    dynamicUpdate('2') !== 2 || dynamicUpdate(1n) !== 1n) {\n"
        "  throw new Error('native update changed semantics');\n"
        "}\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("native_number_update", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);

    char* mir = read_fixture_mir("native_number_update");
    ASSERT_NE(mir, nullptr);
    const char* native_end = NULL;
    const char* native = find_mir_function(mir, "_js_nativeUpdate_", &native_end);
    ASSERT_NE(native, nullptr);
    ASSERT_NE(native_end, nullptr);
    EXPECT_NE(strstr(native, "\n\tdadd\t"), nullptr);
    EXPECT_NE(strstr(native, "\n\tdsub\t"), nullptr);
    const char* increment = strstr(native, "\n\tcall\tjs_increment");
    const char* decrement = strstr(native, "\n\tcall\tjs_decrement");
    EXPECT_FALSE(increment && increment < native_end);
    EXPECT_FALSE(decrement && decrement < native_end);
    free(mir);
    expect_trace_off_same("native_number_update", source, output);
}

TEST(JsOpt, NativeAliasCompoundAssignmentKeepsGenericSemantics) {
    const char* source =
        "function subtractLoop(x, y) {\n"
        "  let r = x; let q = 0;\n"
        "  while (r >= y) { r -= y; q++; }\n"
        "  return q;\n"
        "}\n"
        "function chainedSubtract(x, y) {\n"
        "  let first = x; let cursor = first; let count = 0;\n"
        "  while (cursor >= y) { cursor -= y; count++; }\n"
        "  return count;\n"
        "}\n"
        "function assignedSubtract(x, y) {\n"
        "  let first = x; let cursor = 0; cursor = first; let count = 0;\n"
        "  while (cursor >= y) { cursor -= y; count++; }\n"
        "  return count;\n"
        "}\n"
        "function overwrittenAlias(x, y) {\n"
        "  let cursor = 0; cursor = x; cursor = '12'; return cursor >= y;\n"
        "}\n"
        "if (subtractLoop(12, 3) !== 4 || subtractLoop('12', 3) !== 4 ||\n"
        "    subtractLoop('12', '3') !== 0 || subtractLoop(12n, 3n) !== 4) {\n"
        "  throw new Error('alias compound assignment changed semantics');\n"
        "}\n"
        "if (chainedSubtract(12, 3) !== 4 || chainedSubtract('12', 3) !== 4 ||\n"
        "    chainedSubtract('12', '3') !== 0 || chainedSubtract(12n, 3n) !== 4) {\n"
        "  throw new Error('chained alias compound assignment changed semantics');\n"
        "}\n"
        "if (assignedSubtract(12, 3) !== 4 || assignedSubtract('12', 3) !== 4 ||\n"
        "    assignedSubtract('12', '3') !== 0 || assignedSubtract(12n, 3n) !== 4 ||\n"
        "    !overwrittenAlias('not-a-number', 3)) {\n"
        "  throw new Error('assigned alias compound assignment changed semantics');\n"
        "}\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("native_alias_compound", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);

    char* mir = read_fixture_mir("native_alias_compound");
    ASSERT_NE(mir, nullptr);
    const char* native_end = NULL;
    const char* native = find_mir_function(mir, "_js_subtractLoop_", &native_end);
    ASSERT_NE(native, nullptr);
    ASSERT_NE(native_end, nullptr);
    EXPECT_NE(strstr(native, "\n\tdsub\t"), nullptr);
    const char* subtract = strstr(native, "\n\tcall\tjs_subtract");
    EXPECT_FALSE(subtract && subtract < native_end);
    const char* chained_end = NULL;
    const char* chained = find_mir_function(mir, "_js_chainedSubtract_",
        &chained_end);
    ASSERT_NE(chained, nullptr);
    ASSERT_NE(chained_end, nullptr);
    EXPECT_NE(strstr(chained, "\n\tdsub\t"), nullptr);
    const char* chained_subtract = strstr(chained, "\n\tcall\tjs_subtract");
    EXPECT_FALSE(chained_subtract && chained_subtract < chained_end);
    const char* assigned_end = NULL;
    const char* assigned = find_mir_function(mir, "_js_assignedSubtract_",
        &assigned_end);
    ASSERT_NE(assigned, nullptr);
    ASSERT_NE(assigned_end, nullptr);
    EXPECT_NE(strstr(assigned, "\n\tdsub\t"), nullptr);
    const char* assigned_subtract = strstr(assigned, "\n\tcall\tjs_subtract");
    EXPECT_FALSE(assigned_subtract && assigned_subtract < assigned_end);
    const char* overwritten_end = NULL;
    const char* overwritten = find_mir_function(mir, "_js_overwrittenAlias_",
        &overwritten_end);
    ASSERT_NE(overwritten, nullptr);
    ASSERT_NE(overwritten_end, nullptr);
    const char* overwritten_compare = strstr(overwritten, "\n\tcall\tjs_compare");
    EXPECT_TRUE(overwritten_compare && overwritten_compare < overwritten_end);
    free(mir);
    expect_trace_off_same("native_alias_compound", source, output);
}

TEST(JsOpt, RuntimeHelperCensusKeepsGenericCoercionAndIndexSemantics) {
    const char* source =
        "function bump(value) { return value++; }\n"
        "function lower(value) { return --value; }\n"
        "function ordered(left, right) { return left < right; }\n"
        "function integralIndex(value) { const index = 1.0; return value[index]; }\n"
        "function indexed(value) { const index = 1.5; return value[index]; }\n"
        "let coercions = 0;\n"
        "let two = { valueOf() { coercions++; return 2; } };\n"
        "let five = { valueOf() { coercions++; return 5; } };\n"
        "let values = [7, 9]; values[1.5] = 'fractional';\n"
        "if (bump(two) !== 2 || lower(five) !== 4 || !ordered(two, five) ||\n"
        "    integralIndex(values) !== 9 || indexed(values) !== 'fractional' ||\n"
        "    coercions !== 4) {\n"
        "  throw new Error('runtime helper census changed generic semantics');\n"
        "}\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("runtime_helper_census", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_TO_NUMERIC_CALL][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_INCREMENT_CALL][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_DECREMENT_CALL][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_BOXED_COMPARE_CALL][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_NUMBER_INDEX_GET_CALL][1], 0u);
    uint64_t classified_number_reads =
        trace.events[JS_OPT_RUNTIME_NUMBER_INDEX_NUMERIC_ARRAY][1] +
        trace.events[JS_OPT_RUNTIME_NUMBER_INDEX_TAGGED_ARRAY][1] +
        trace.events[JS_OPT_RUNTIME_NUMBER_INDEX_TYPED_ARRAY][1] +
        trace.events[JS_OPT_RUNTIME_NUMBER_INDEX_OTHER][1];
    EXPECT_GT(classified_number_reads, 0u);
    expect_trace_off_same("runtime_helper_census", source, output);
}

TEST(JsOpt, DiscardedGenericUpdateFusesToNumericAndUpdate) {
    const char* source =
        "function genericLoop(value) {\n"
        "  for (let index = 0; index < 4; index++) value++;\n"
        "  return value;\n"
        "}\n"
        "let coercions = 0;\n"
        "let value = { valueOf() { coercions++; return 2; } };\n"
        "if (genericLoop('2') !== 6 || genericLoop(value) !== 6 || coercions !== 1)\n"
        "  throw new Error('discarded update changed coercion or result');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("discarded_generic_update", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_TO_NUMERIC_CALL][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_INCREMENT_CALL][1], 0u);

    char* mir = read_fixture_mir("discarded_generic_update");
    ASSERT_NE(mir, nullptr);
    const char* generic_end = NULL;
    const char* generic = find_mir_function(mir, "_js_genericLoop_", &generic_end,
        "_body:\tfunc");
    ASSERT_NE(generic, nullptr);
    ASSERT_NE(generic_end, nullptr);
    const char* update = strstr(generic, "\n\tcall\tjs_increment");
    EXPECT_TRUE(update && update < generic_end);
    const char* numeric_update = strstr(generic, "\n\tcall\tjs_increment_numeric");
    EXPECT_FALSE(numeric_update && numeric_update < generic_end);
    const char* numeric = strstr(generic, "\n\tcall\tjs_to_numeric");
    EXPECT_FALSE(numeric && numeric < generic_end);
    free(mir);
    expect_trace_off_same("discarded_generic_update", source, output);
}

TEST(JsOpt, CompanionDenseReadPreservesHoles) {
    const char* source =
        "function middle(values) {\n"
        "  let result = undefined;\n"
        "  for (let index = 0; index < 3; index++) {\n"
        "    if (index === 1) result = values[index];\n"
        "  }\n"
        "  return result;\n"
        "}\n"
        "let dense = [1, 2, 3]; dense.note = 'companion';\n"
        "let hole = new Array(3); hole[0] = 1; hole[2] = 3; hole.note = 'companion';\n"
        "if (middle(dense) !== 2 || middle(hole) !== undefined)\n"
        "  throw new Error('companion dense read changed own-element behavior');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("companion_dense_read", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_OWN_ELEMENT_GET_HIT][1], 0u);

    char* mir = read_fixture_mir("companion_dense_read");
    ASSERT_NE(mir, nullptr);
    const char* middle_end = NULL;
    const char* middle = find_mir_function(mir, "_js_middle_", &middle_end,
        "_body:\tfunc");
    ASSERT_NE(middle, nullptr);
    ASSERT_NE(middle_end, nullptr);
    const char* companion = strstr(middle,
        "js_array_get_existing_own_dense_with_props_or_missing");
    EXPECT_TRUE(companion && companion < middle_end);
    const char* fallback = strstr(middle, "js_elements_get_number");
    EXPECT_TRUE(fallback && fallback < middle_end);
    free(mir);
    expect_trace_off_same("companion_dense_read", source, output);
}

TEST(JsOpt, MirNativeNumberIndexKeepsKeyUnboxed) {
    const char* source =
        "function nativeRead(value) { return value[0]; }\n"
        "function numericSum(value) {\n"
        "  let total = 0; for (let i = 0; i < value.length; i++) total += value[i];\n"
        "  return total;\n"
        "}\n"
        "function negativeZeroRead(value) { return value[-0]; }\n"
        "function negativeRead(value) { return value[-1]; }\n"
        "function fractionalRead(value) { const index = 1.5; return value[index]; }\n"
        "function nanRead(value) { return value[0 / 0]; }\n"
        "function infinityRead(value) { return value[1 / 0]; }\n"
        "function largeRead(value) { return value[4294967295]; }\n"
        "function stringRead(value) { return value[1]; }\n"
        "function partialRead(value, index) { return value[index]; }\n"
        "function packedTaggedRead() {\n"
        "  const values = ['dense', 'read']; let text = '';\n"
        "  for (let i = 0; i < values.length; i++) text += values[i]; return text;\n"
        "}\n"
        "function accessorFallbackRead() {\n"
        "  const values = ['stale']; Object.defineProperty(values, '0', {\n"
        "    get: function() { return 'accessor'; }, configurable: true });\n"
        "  return values[0];\n"
        "}\n"
        "var array = [4, 5]; array[-1] = 'negative'; array[1.5] = 'fractional';\n"
        "array[NaN] = 'nan'; array[Infinity] = 'infinity'; array[4294967295] = 'large';\n"
        "var hole = new Array(1); Object.prototype[0] = 'proto-index';\n"
        "var result = nativeRead(array) === 4 && numericSum(array) === 9 &&\n"
        "  numericSum(new Uint8Array([4, 5])) === 9 && fractionalRead(array) === 'fractional' &&\n"
        "  negativeZeroRead(array) === 4 && negativeRead(array) === 'negative' &&\n"
        "  nanRead(array) === 'nan' && infinityRead(array) === 'infinity' &&\n"
        "  largeRead(array) === 'large' && largeRead(new Uint8Array(1)) === undefined &&\n"
        "  stringRead('abc') === 'b' && partialRead(array, 1) === 5 &&\n"
        "  packedTaggedRead() === 'denseread' && accessorFallbackRead() === 'accessor' &&\n"
        "  nativeRead(hole) === 'proto-index';\n"
        "delete Object.prototype[0];\n"
        "if (!result) throw new Error('native index changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("mir_native_index", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_MIR_NATIVE_INDEX_ADMITTED][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_MIR_NATIVE_INDEX_FALLBACK][2], 0u);
    EXPECT_GT(trace.events[JS_OPT_MIR_DENSE_INDEX_ADMITTED][1], 0u);

    char* mir = read_fixture_mir("mir_native_index");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_elements_get_number"), nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_get_reference"), nullptr);
    EXPECT_NE(strstr(mir, "\tlsh\t"), nullptr);
    free(mir);
    expect_trace_off_same("mir_native_index", source, output);
}

TEST(JsOpt, MirLiteralFieldPlanUsesExactShape) {
    const char* source =
        "function literalFields() {\n"
        "  const point = { x: 7.5, label: 'field' };\n"
        "  return point.x + ':' + point.label;\n"
        "}\n"
        "function makeReturnedPoint() { return { x: 3.5, label: 'returned' }; }\n"
        "function returnedFields() {\n"
        "  return makeReturnedPoint().x + ':' + makeReturnedPoint().label;\n"
        "}\n"
        "function consumeDirectArgument(point) { return point.x + ':' + point.label; }\n"
        "function directArgumentFields() {\n"
        "  return consumeDirectArgument({ x: 5.5, label: 'argument' });\n"
        "}\n"
        "function typeTransitionFallback() {\n"
        "  const point = { x: 1.5 }; point.x = 'changed'; return point.x;\n"
        "}\n"
        "function directStore() {\n"
        "  const point = { x: 1.5 }; point.x = 2.5; return point.x;\n"
        "}\n"
        "function frozenStoreFallback() {\n"
        "  const point = { x: 1.5 }; Object.freeze(point); point.x = 2.5; return point.x;\n"
        "}\n"
        "function accessorFallback() {\n"
        "  const point = { x: 1.5 }; let stored = 'accessor'; Object.defineProperty(point, 'x', {\n"
        "    get: function() { return stored; }, set: function(value) { stored = value; }, configurable: true });\n"
        "  point.x = 'setter';\n"
        "  return point.x;\n"
        "}\n"
        "class ClassFieldPlan {\n"
        "  x = 6.5; label = 'class';\n"
        "  read() { return this.x + ':' + this.label; }\n"
        "  store() { this.x = 8.5; return this.x; }\n"
        "}\n"
        "function classFields() {\n"
        "  const point = new ClassFieldPlan(); return point.read() + ':' + point.store();\n"
        "}\n"
        "function classAccessorEscapeFallback() {\n"
        "  const point = new ClassFieldPlan();\n"
        "  Object.defineProperty(point, 'x', { get: function() { return 'escaped'; }, configurable: true });\n"
        "  return point.read();\n"
        "}\n"
        "if (literalFields() !== '7.5:field' || returnedFields() !== '3.5:returned' ||\n"
        "    directArgumentFields() !== '5.5:argument' ||\n"
        "    directStore() !== 2.5 ||\n"
        "    typeTransitionFallback() !== 'changed' || frozenStoreFallback() !== 1.5 ||\n"
        "    accessorFallback() !== 'setter' || classFields() !== '6.5:class:8.5' ||\n"
        "    classAccessorEscapeFallback() !== 'escaped:class') throw new Error('field plan changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("mir_literal_field", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_MIR_LITERAL_FIELD_ADMITTED][1], 0u);

    char* mir = read_fixture_mir("mir_literal_field");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "js_new_literal_object_with_typemap"), nullptr);
    EXPECT_NE(strstr(mir, "js_set_class_instance_shape"), nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_get_name_id"), nullptr);
    free(mir);
    expect_trace_off_same("mir_literal_field", source, output);
}

TEST(JsOpt, StringLeavesProfileAsciiAndUnicode) {
    const char* source =
        "var ascii = 'ab,cd'; var unicode = 'A😀,B';\n"
        "var value = ascii.indexOf('b') + ascii.split(',').length + ascii.slice(1).length +\n"
        "  ascii.charCodeAt(0) + (ascii + 'z').length;\n"
        "value += unicode.indexOf('😀') + unicode.split(',').length + unicode.slice(1).length +\n"
        "  unicode.charCodeAt(1) + (unicode + 'z').length;\n"
        "var escaped = '%' + 'F'; escaped = escaped + '0'; escaped = escaped + '%';\n"
        "escaped = escaped + '9'; escaped = escaped + 'F'; escaped = escaped + '%';\n"
        "escaped = escaped + '9'; escaped = escaped + '9'; escaped = escaped + '%';\n"
        "escaped = escaped + '8'; escaped = escaped + '2';\n"
        "var decoded = decodeURIComponent(escaped);\n"
        "if (!(value > 0) || decoded.length !== 2 || decoded.charCodeAt(0) !== 55357 ||\n"
        "    decoded.charCodeAt(1) !== 56898)\n"
        "  throw new Error('string leaf profile changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("string_leaves", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_STRING_SEARCH_ASCII][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_SEARCH_UNICODE][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_SPLIT_ASCII][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_SPLIT_UNICODE][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_SLICE_ASCII][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_SLICE_UNICODE][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_CHAR_ACCESS_ASCII][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_CHAR_ACCESS_UNICODE][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_CONCAT_ASCII][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_STRING_CONCAT_UNICODE][1], 0u);
    expect_trace_off_same("string_leaves", source, output);
}

TEST(JsOpt, AsciiSubstringValueCacheReusesLeaves) {
    const char* source =
        "var source = 'alpha:bravo'; var text = '';\n"
        "for (var i = 0; i < 4; i++) text += source.slice(0, 5);\n"
        "if (text !== 'alphaalphaalphaalpha') throw new Error('substring cache changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("ascii_substring_cache", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_ASCII_SUBSTRING_CACHE_MISS][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_ASCII_SUBSTRING_CACHE_HIT][1], 0u);
    expect_trace_off_same("ascii_substring_cache", source, output);
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
    // D1.3: known Number keys use the native-key element kernel; the write
    // stays on the canonical Set completion path.
    EXPECT_NE(strstr(mir, "js_elements_get_number"), nullptr);
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
    // D1.3: the guarded setter delegates typed-index semantics to one runtime
    // leaf; a non-typed receiver still uses the shared generic Set fallback.
    EXPECT_NE(strstr(mir, "js_get_reference"), nullptr);
    EXPECT_NE(strstr(mir, "js_set"), nullptr);
    EXPECT_NE(strstr(mir, "js_typed_array_set_number_if_kind"), nullptr);
    EXPECT_EQ(strstr(mir, "js_typed_array_matches_type"), nullptr);
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

TEST(JsOpt, OrdinaryEnumerationInspectsDescriptorsWithoutMaterializingThem) {
    const char* source =
        "var proto = { inherited: 7 };\n"
        "var object = Object.create(proto); object.own = 1;\n"
        "Object.defineProperty(object, 'hidden', { value: 2, enumerable: false });\n"
        "var objectKeys = []; for (var key in object) objectKeys.push(key);\n"
        "var values = Object.values(object).join(',');\n"
        "var array = [3, 4]; array.extra = 5;\n"
        "Object.defineProperty(array, '1', { enumerable: false });\n"
        "var arrayKeys = []; for (var index in array) arrayKeys.push(index);\n"
        "var arrayValues = Object.values(array).join(',');\n"
        "if (objectKeys.join(',') !== 'own,inherited' || values !== '1' ||\n"
        "    arrayKeys.join(',') !== '0,extra' || arrayValues !== '3,5')\n"
        "  throw new Error('ordinary enumeration changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("ordinary_enumeration_inspect", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_OWN_ENUMERABILITY_INSPECT][1], 0u);
    expect_trace_off_same("ordinary_enumeration_inspect", source, output);
}

TEST(JsOpt, StaticNumericArrayLiteralUsesCompactInitializer) {
    const char* source =
        "var first = [0, 1.5, 2, 3];\n"
        "var second = [0, 1.5, 2, 3];\n"
        "var nested = [[4, 5, 6], [7, 8, 9]];\n"
        "first[1] = 11; nested[0][0] = 12;\n"
        "if (second[1] !== 1.5 || nested[1][2] !== 9 ||\n"
        "    first.join(',') !== '0,11,2,3' || nested[0][0] !== 12)\n"
        "  throw new Error('static numeric array literal changed identity');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("static_numeric_array_literal", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_STATIC_NUMERIC_ARRAY_INITIALIZER][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_ARRAY_GC_ITEMS_ALLOC][1], 0u);
    EXPECT_EQ(trace.events[JS_OPT_ARRAY_RUNTIME_ITEMS_INSTALL][1], 0u);

    char* mir = read_fixture_mir("static_numeric_array_literal");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "js_array_new_from_static_items"), nullptr);
    free(mir);
    expect_trace_off_same("static_numeric_array_literal", source, output);
}

TEST(JsOpt, StaticPrimitiveObjectLiteralUsesCompactInitializer) {
    const char* source =
        "var first = { number: 1.5, label: 'first', enabled: true, absent: null, number: 2 };\n"
        "var second = { number: 1.5, label: 'first', enabled: true, absent: null, number: 2 };\n"
        "first.number = 9; first.label = 'changed'; first.extra = 7;\n"
        "var descriptor = Object.getOwnPropertyDescriptor(second, 'number');\n"
        "if (second.number !== 2 || second.label !== 'first' || !second.enabled ||\n"
        "    second.absent !== null || Object.keys(second).join(',') !==\n"
        "    'number,label,enabled,absent' || !descriptor.writable ||\n"
        "    !descriptor.enumerable || !descriptor.configurable || first === second)\n"
        "  throw new Error('static object literal changed identity or properties');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("static_primitive_object_literal", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_STATIC_OBJECT_INITIALIZER][1], 0u);

    char* mir = read_fixture_mir("static_primitive_object_literal");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "js_object_new_from_static_properties"), nullptr);
    free(mir);
    expect_trace_off_same("static_primitive_object_literal", source, output);
}

TEST(JsOpt, StaticCompositeLiteralRecipePreservesFreshNestedValues) {
    const char* source =
        "function makeValue() {\n"
        "  return { nested: { list: [1, , 3], marker: 'first', marker: 'last' }, ready: true };\n"
        "}\n"
        "var first = makeValue(); var second = makeValue();\n"
        "first.nested.list[0] = 9; first.nested.marker = 'changed';\n"
        "if (second === first || second.nested === first.nested ||\n"
        "    second.nested.list[0] !== 1 || (1 in second.nested.list) ||\n"
        "    second.nested.list[2] !== 3 || second.nested.marker !== 'last' ||\n"
        "    Object.keys(second).join(',') !== 'nested,ready')\n"
        "  throw new Error('static composite literal recipe changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("static_composite_literal_recipe", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);

    expect_trace_off_same("static_composite_literal_recipe", source, output);
}

TEST(JsOpt, ConstructorAssignedFieldsUseGuardedLayout) {
    const char* source =
        "class ConstructorAssignedPlan {\n"
        "  constructor() { this.x = 6.5; this.label = 'constructor'; }\n"
        "  read() { return this.x + ':' + this.label; }\n"
        "  store() { this.x = 8.5; return this.x; }\n"
        "}\n"
        "var point = new ConstructorAssignedPlan();\n"
        "if (Object.keys(point).join(',') !== 'x,label' ||\n"
        "    point.read() !== '6.5:constructor' || point.store() !== 8.5)\n"
        "  throw new Error('constructor layout changed source-order properties');\n"
        "Object.defineProperty(point, 'x', { get: function() { return 'escaped'; }, configurable: true });\n"
        "if (point.read() !== 'escaped:constructor')\n"
        "  throw new Error('constructor layout skipped accessor fallback');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("constructor_assigned_fields", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_MIR_LITERAL_FIELD_ADMITTED][1], 0u);

    char* mir = read_fixture_mir("constructor_assigned_fields");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "js_set_class_instance_shape"), nullptr);
    EXPECT_NE(strstr(mir, "js_set_name_id"), nullptr);
    free(mir);
    expect_trace_off_same("constructor_assigned_fields", source, output);
}

TEST(JsOpt, LoopStableModuleNameIdSurvivesNestedEval) {
    const char* source =
        "function nameIdLoop(receiver) {\n"
        "  var total = 0;\n"
        "  for (let index = 0; index < 8; index += 1)\n"
        "    total += receiver.tune13ModuleNameField;\n"
        "  return total;\n"
        "}\n"
        "var getterCalls = 0; var receiver = {};\n"
        "Object.defineProperty(receiver, 'tune13ModuleNameField', {\n"
        "  get: function() {\n"
        "    getterCalls += 1;\n"
        "    if (getterCalls === 4) eval('var tune13NestedEval = 1');\n"
        "    return 2;\n"
        "  }, configurable: true });\n"
        "if (nameIdLoop(receiver) !== 16 || getterCalls !== 8)\n"
        "  throw new Error('loop name id changed nested-eval property access');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("loop_stable_module_name_id", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_MIR_LOOP_STABLE_NAME_ID][1], 0u);

    char* mir = read_fixture_mir("loop_stable_module_name_id");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "lambda_active_module_name_id"), nullptr);
    free(mir);
    expect_trace_off_same("loop_stable_module_name_id", source, output);
}

TEST(JsOpt, MirLightCallKeepsDynamicFunctionSemantics) {
    const char* source =
        "function plus(left, right) { return left + right; }\n"
        "function receiverValue() { return this.marker; }\n"
        "function argumentValue() { return arguments[0]; }\n"
        "var invoke = plus; var total = 0;\n"
        "for (var index = 0; index < 24; index += 1) total = invoke(total, index);\n"
        "var receiver = { marker: 17 }; var readReceiver = receiverValue;\n"
        "var readArgument = argumentValue;\n"
        "if (total !== 276 || readReceiver.call(receiver) !== 17 ||\n"
        "    readArgument(23) !== 23)\n"
        "  throw new Error('light call changed dynamic function semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("mir_light_call", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_MIR_LIGHT_CALL][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_MIR_LIGHT_DIRECT_ACTIVATION][1], 0u);

    char* mir = read_fixture_mir("mir_light_call");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "js_call"), nullptr);
    free(mir);
    expect_trace_off_same("mir_light_call", source, output);
}

TEST(JsOpt, ReceiverOnlyBoundCallForwardsRootedArguments) {
    const char* source =
        "function add(left, right) { return this.base + left + right; }\n"
        "var bound = add.bind({ base: 3 });\n"
        "var rebound = bound.bind({ base: 99 });\n"
        "var withPrefix = add.bind({ base: 3 }, 4);\n"
        "if (bound(5, 6) !== 14 || rebound(1, 2) !== 6 || withPrefix(5) !== 12)\n"
        "  throw new Error('bound call forwarding changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("bound_call_forward_args", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_BOUND_CALL_FORWARD_ARGS][1], 0u);
    expect_trace_off_same("bound_call_forward_args", source, output);
}

TEST(JsOpt, PackedNumberStoreKeepsNativeWriteAndAccessorMiss) {
    const char* source =
        "function updateNumbers(values) {\n"
        "  for (let index = 0; index < values.length; index += 1)\n"
        "    values[index] = values[index] + 0.5;\n"
        "  return values[0] + values[1] + values[2];\n"
        "}\n"
        "var values = [1.5, 2.5, 3.5];\n"
        "if (updateNumbers(values) !== 9)\n"
        "  throw new Error('packed Number store changed direct update');\n"
        "var observed = '';\n"
        "Object.defineProperty(values, '1', {\n"
        "  get: function() { observed += 'g'; return 9.5; },\n"
        "  set: function(value) { observed += 's' + value; }, configurable: true });\n"
        "updateNumbers(values);\n"
        "if (observed !== 'gs10g' || values[0] !== 2.5 || values[2] !== 4.5)\n"
        "  throw new Error('packed Number store skipped accessor miss');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("packed_number_store", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_ORDINARY_NUMBER_STORE][1], 0u);

    char* mir = read_fixture_mir("packed_number_store");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "js_array_set_existing_number_no_gc"), nullptr);
    free(mir);
    expect_trace_off_same("packed_number_store", source, output);
}

TEST(JsOpt, NavierStokesWriteReferenceSurvivesRhsIndexUpdates) {
    char* benchmark = read_text("test/benchmark/jetstream/navier-stokes.js");
    ASSERT_NE(benchmark, nullptr);
    const char* frames =
        "\nfor (var frame = 0; frame < 15; frame++) runNavierStokes();\n"
        "console.log('OPT_OK');\n";
    size_t source_size = strlen(benchmark) + strlen(frames) + 1;
    char* source = (char*)malloc(source_size);
    ASSERT_NE(source, nullptr);
    snprintf(source, source_size, "%s%s", benchmark, frames);
    free(benchmark);

    char output[4096];
    // The canonical 15-frame workload exercises the checksum in a debug host.
    ASSERT_TRUE(run_fixture_mode("navier_write_reference", source, false,
        NULL, output, sizeof(output), 120000));
    expect_ok_output(output);
    free(source);
}

TEST(JsOpt, WriteReferenceRetainsClosureMutation) {
    const char* source =
        "function directWrite() {\n"
        "  let values = [0, 0];\n"
        "  let index = 0;\n"
        "  values[index] = ++index;\n"
        "  return values[0] === 1 && values[1] === 0;\n"
        "}\n"
        "function closureWrite() {\n"
        "  let values = [0, 0];\n"
        "  let index = 0;\n"
        "  function move() { index = 1; return 7; }\n"
        "  values[index] = move();\n"
        "  return values[0] === 7 && values[1] === 0;\n"
        "}\n"
        "if (!directWrite() || !closureWrite())\n"
        "  throw new Error('write Reference changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("write_reference_rhs", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    expect_trace_off_same("write_reference_rhs", source, output);
}

TEST(JsOpt, PackedNumericStrictEqualityKeepsDirectNumberLoads) {
    const char* source =
        "function equalCount(left, right) {\n"
        "  var hits = 0;\n"
        "  for (let index = 0; index < left.length; index += 1) {\n"
        "    if (left[index] === right[index]) hits += 1;\n"
        "  }\n"
        "  return hits;\n"
        "}\n"
        "var left = [1, 2, NaN, -0];\n"
        "var right = [1, 9, NaN, 0];\n"
        "if (equalCount(left, right) !== 2) throw new Error('numeric strict equality changed');\n"
        "var getterCalls = 0;\n"
        "Object.defineProperty(left, '1', { get: function() { getterCalls += 1; return 9; }, configurable: true });\n"
        "if (equalCount(left, right) !== 3 || getterCalls !== 1)\n"
        "  throw new Error('packed strict equality skipped generic miss');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("packed_numeric_strict_equality", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_MIR_DENSE_INDEX_ADMITTED][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_MIR_PACKED_STRICT_EQUAL][1], 0u);

    char* mir = read_fixture_mir("packed_numeric_strict_equality");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "js_strict_equal"), nullptr);
    free(mir);
    expect_trace_off_same("packed_numeric_strict_equality", source, output);
}

static char* make_large_static_numeric_array_source() {
    const int length = 10001;
    const char* prefix = "var values = [";
    const char* suffix = "];\n"
        "if (values.length !== 10001 || values[0] !== 1 || values[10000] !== 1) "
        "throw new Error('large compact initializer changed values');\n"
        "console.log('OPT_OK');\n";
    size_t capacity = strlen(prefix) + (size_t)length * 2 + strlen(suffix) + 1;
    char* source = (char*)malloc(capacity);
    if (!source) return NULL;
    char* out = source;
    memcpy(out, prefix, strlen(prefix));
    out += strlen(prefix);
    for (int index = 0; index < length; index++) {
        *out++ = '1';
        if (index + 1 < length) *out++ = ',';
    }
    memcpy(out, suffix, strlen(suffix) + 1);
    return source;
}

TEST(JsOpt, StaticNumericArrayLiteralInitializesSparseLengthArray) {
    char* source = make_large_static_numeric_array_source();
    ASSERT_NE(source, nullptr);
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("static_numeric_array_sparse_length", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_STATIC_NUMERIC_ARRAY_INITIALIZER][1], 0u);
    expect_trace_off_same("static_numeric_array_sparse_length", source, output);
    free(source);
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

TEST(JsOpt, HostDynamicReadsRemainGlobalOnly) {
    const char* source =
        "globalThis.event = 17;\n"
        "var ordinary = { event: 23, innerWidth: 29, length: 31 };\n"
        "function read(object) { return object.event + ':' + object.innerWidth + ':' + object.length; }\n"
        "if (globalThis.event !== 17 || read(ordinary) !== '23:29:31')\n"
        "  throw new Error('host dynamic receiver changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("host_dynamic_global_only", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.reasons[JS_OPT_REASON_NAMED_FAST_HOST_DYNAMIC], 0u);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_HIT][1], 0u);
    expect_trace_off_same("host_dynamic_global_only", source, output);
}

TEST(JsOpt, NamedDataDescriptorReadSkipsOnlyIrrelevantAttributes) {
    const char* source =
        "function read(object) { return object.hidden + object.fixed; }\n"
        "function readGetter(object) { return object.getter; }\n"
        "var object = {}; var getterCalls = 0;\n"
        "Object.defineProperty(object, 'hidden', { value: 7, enumerable: false });\n"
        "Object.defineProperty(object, 'fixed', { value: 8, writable: false, configurable: false });\n"
        "Object.defineProperty(object, 'getter', { get: function() { getterCalls += 1; return 9; } });\n"
        "if (read(object) !== 15 || readGetter(object) !== 9 || getterCalls !== 1)\n"
        "  throw new Error('descriptor read changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("named_data_descriptor_read", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_DATA_DESCRIPTOR][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_MISS][2], 0u);
    expect_trace_off_same("named_data_descriptor_read", source, output);
}

TEST(JsOpt, DynamicPropertyKeyKeepsOneCanonicalNameThroughPrototypeGet) {
    const char* source =
        "function read(object, suffix) { return object['com' + suffix]; }\n"
        "var getterCalls = 0;\n"
        "var prototype = {};\n"
        "Object.defineProperty(prototype, 'computed', { get: function() {\n"
        "  getterCalls += 1; return 17; }, configurable: true });\n"
        "var child = Object.create(prototype);\n"
        "if (read(child, 'puted') !== 17 || getterCalls !== 1)\n"
        "  throw new Error('dynamic property key changed inherited Get');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("dynamic_property_key_prototype_get", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);

    char* mir = read_fixture_mir("dynamic_property_key_prototype_get");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_get_reference"), nullptr);
    free(mir);
    expect_trace_off_same("dynamic_property_key_prototype_get", source, output);
}

TEST(JsOpt, NamedFunctionDataReadPreservesStrictPoisonPills) {
    const char* source =
        "function target(left, right) { return left + right; }\n"
        "function strictTarget() { 'use strict'; }\n"
        "function readMetadata(fn) { return fn.name + ':' + fn.length; }\n"
        "function readCaller(fn) { try { return fn.caller; } catch (error) { return error.name; } }\n"
        "if (readMetadata(target) !== 'target:2' || readCaller(strictTarget) !== 'TypeError')\n"
        "  throw new Error('function metadata read changed semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("named_function_data_read", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_FUNCTION_DATA][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_MISS][2], 0u);
    expect_trace_off_same("named_function_data_read", source, output);
}

TEST(JsOpt, ArrayLengthNameUsesOwnNoGcHead) {
    const char* source =
        "function direct(values) { return values.length; }\n"
        "function computed(values) { return values['length']; }\n"
        "function content() { return arguments.length; }\n"
        "if (direct([1, 2, 3]) !== 3 || computed([4, 5]) !== 2 ||\n"
        "    content(6, 7, 8, 9) !== 4) throw new Error('array length changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("array_length_name_head", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_HIT][1], 0u);

    char* mir = read_fixture_mir("array_length_name_head");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_get_name_id"), nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_get_reference"), nullptr);
    free(mir);
    expect_trace_off_same("array_length_name_head", source, output);
}

TEST(JsOpt, PrimitiveStringLengthUsesOwnNoGcHead) {
    const char* source =
        "function direct(text) { return text.length; }\n"
        "function computed(text) { return text['length']; }\n"
        "var astral = String.fromCodePoint(0x1f642);\n"
        "if (direct('abc') !== 3 || direct(astral) !== 2 ||\n"
        "    computed('four') !== 4 || direct(new String('abc')) !== 3)\n"
        "  throw new Error('string length changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("primitive_string_length_name_head", source,
                            &trace, output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_NAMED_FAST_STRING_LENGTH][1], 0u);

    char* mir = read_fixture_mir("primitive_string_length_name_head");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_get_name_id"), nullptr);
    free(mir);
    expect_trace_off_same("primitive_string_length_name_head", source,
                          output);
}

TEST(JsOpt, PrimitiveStringConcatAvoidsGenericAddSetup) {
    const char* source =
        "function concat(left, right) { return left + right; }\n"
        "var coercions = 0;\n"
        "var object = { toString: function() { coercions += 1; return 'object'; } };\n"
        "if (concat('left', 'right') !== 'leftright' ||\n"
        "    concat('prefix:', object) !== 'prefix:object' || coercions !== 1)\n"
        "  throw new Error('string concat changed coercion semantics');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("primitive_string_concat_head", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_STRING_CONCAT_HEAD][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_RUNTIME_NUMBER_HEAD_FALLBACK][2], 0u);

    char* mir = read_fixture_mir("primitive_string_concat_head");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "call\tjs_add"), nullptr);
    free(mir);
    expect_trace_off_same("primitive_string_concat_head", source, output);
}

TEST(JsOpt, DeferredMirFunctionPublicationKeepsFinalMetadata) {
    const char* source =
        "function make(prefix) {\n"
        "  return function(first, second) { return prefix + first + second; };\n"
        "}\n"
        "var first = make('a');\n"
        "var second = make('b');\n"
        "var desc = Object.getOwnPropertyDescriptor(first, 'length');\n"
        "if (first('1', '2') !== 'a12' || second('3', '4') !== 'b34' ||\n"
        "    first.length !== 2 || !desc || desc.value !== 2 ||\n"
        "    desc.writable || desc.enumerable || !desc.configurable)\n"
        "  throw new Error('deferred function metadata changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("deferred_mir_function_metadata", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GE(trace.events[JS_OPT_MIR_DEFERRED_FUNCTION_FINALIZE][1], 3u);
    EXPECT_GE(trace.events[JS_OPT_MIR_LAZY_FUNCTION_METADATA][1], 3u);
    expect_trace_off_same("deferred_mir_function_metadata", source, output);
}

TEST(JsOpt, LazyMirFunctionMetadataPreservesReflectionAndDeletion) {
    const char* source =
        "function sample(first, second) { return first + second; }\n"
        "if (!Object.hasOwn(sample, 'name') || !Object.hasOwn(sample, 'length') ||\n"
        "    sample.name !== 'sample' || sample.length !== 2)\n"
        "  throw new Error('lazy function metadata read changed');\n"
        "var names = Object.getOwnPropertyNames(sample);\n"
        "if (names.indexOf('length') < 0 || names.indexOf('name') < 0 ||\n"
        "    names.indexOf('length') > names.indexOf('name'))\n"
        "  throw new Error('lazy function metadata key order changed');\n"
        "var descriptor = Object.getOwnPropertyDescriptor(sample, 'name');\n"
        "if (!descriptor || descriptor.value !== 'sample' || descriptor.writable ||\n"
        "    descriptor.enumerable || !descriptor.configurable || !delete sample.name ||\n"
        "    Object.hasOwn(sample, 'name') || sample.name !== '')\n"
        "  throw new Error('lazy function metadata descriptor changed');\n"
        "function prototypeFirst() {}\n"
        "prototypeFirst.prototype; prototypeFirst.extra = 1;\n"
        "var prototypeNames = Object.getOwnPropertyNames(prototypeFirst);\n"
        "if (!(prototypeNames.indexOf('length') < prototypeNames.indexOf('name') &&\n"
        "      prototypeNames.indexOf('name') < prototypeNames.indexOf('prototype') &&\n"
        "      prototypeNames.indexOf('prototype') < prototypeNames.indexOf('extra')))\n"
        "  throw new Error('lazy prototype key order changed');\n"
        "function definedFirst() {}\n"
        "Object.defineProperty(definedFirst, 'extra', { value: 1 });\n"
        "var definedNames = Object.getOwnPropertyNames(definedFirst);\n"
        "if (!(definedNames.indexOf('length') < definedNames.indexOf('name') &&\n"
        "      definedNames.indexOf('name') < definedNames.indexOf('extra') &&\n"
        "      definedNames.indexOf('extra') < definedNames.indexOf('prototype')))\n"
        "  throw new Error('lazy define key order changed');\n"
        "function deletedFirst() {}\n"
        "if (!delete deletedFirst.name || Object.hasOwn(deletedFirst, 'name') ||\n"
        "    deletedFirst.name !== '')\n"
        "  throw new Error('lazy metadata delete changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("lazy_mir_function_metadata", source, &trace,
                            output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_MIR_LAZY_FUNCTION_METADATA][1], 0u);
    expect_trace_off_same("lazy_mir_function_metadata", source, output);
}

TEST(JsOpt, NumericLocalFactsSurviveBoxedAndVoidReturns) {
    const char* source =
        "function boxed(limit) {\n"
        "  let total = 0;\n"
        "  for (let i = 0; i < limit; i++) total += i * 0.5;\n"
        "  return { total: total };\n"
        "}\n"
        "function voidLoop(limit) {\n"
        "  let total = 0;\n"
        "  for (let i = 0; i < limit; i++) total += i * 0.5;\n"
        "  console.log('void:' + total);\n"
        "}\n"
        "function mixed(flag) {\n"
        "  let total = 0;\n"
        "  if (flag) total = 'x';\n"
        "  total += 1;\n"
        "  return total;\n"
        "}\n"
        "console.log('boxed:' + boxed(4).total);\n"
        "voidLoop(4);\n"
        "console.log('mixed:' + mixed(false) + ',' + mixed(true));\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("numeric_locals_boxed_void", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_NE(strstr(output, "boxed:3"), nullptr) << output;
    EXPECT_NE(strstr(output, "void:3"), nullptr) << output;
    EXPECT_NE(strstr(output, "mixed:1,x1"), nullptr) << output;

    char* mir = read_fixture_mir("numeric_locals_boxed_void");
    ASSERT_NE(mir, nullptr);
    // Both functions have boxed public completions; native arithmetic in the
    // body proves local facts are independent of their return ABI.
    EXPECT_NE(strstr(mir, "dadd"), nullptr);
    free(mir);
    expect_trace_off_same("numeric_locals_boxed_void", source, output);
}

TEST(JsOpt, TypedArrayStoresUseGuardedNumericKeyLeaf) {
    const char* source =
        "function fill() {\n"
        "  let bytes = new Uint8Array(4);\n"
        "  let ints = new Int32Array(4);\n"
        "  let floats = new Float64Array(4);\n"
        "  for (let i = 0; i < 4; i++) {\n"
        "    bytes[i] = i + 1;\n"
        "    floats[i] = i + 0.5;\n"
        "  }\n"
        "  ints[0] = -1; ints[1] = 2147483648;\n"
        "  ints[2] = 4294967297.75; ints[3] = -1.75;\n"
        "  bytes[-0] = 9;\n"
        "  return bytes[0] + ':' + bytes[3] + ':' + floats[0] + ':' + floats[3] +\n"
        "    ':' + ints[0] + ':' + ints[1] + ':' + ints[2] + ':' + ints[3];\n"
        "}\n"
        "function reassigned() {\n"
        "  let data = new Uint8Array(1);\n"
        "  data = [0];\n"
        "  data[0] = 7;\n"
        "  return data[0];\n"
        "}\n"
        "function coerciveResize() {\n"
        "  let buffer = new ArrayBuffer(4, { maxByteLength: 4 });\n"
        "  let data = new Int32Array(buffer);\n"
        "  let value = { valueOf() { buffer.resize(0); return 7; } };\n"
        "  data[0] = value;\n"
        "  return data.length + ':' + data[0];\n"
        "}\n"
        "console.log('typed:' + fill());\n"
        "console.log('fallback:' + reassigned());\n"
        "console.log('coercion:' + coerciveResize());\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("typed_array_store_leaf", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_NE(strstr(output, "typed:1:4:0.5:3.5:-1:-2147483648:1:-1"), nullptr) << output;
    EXPECT_NE(strstr(output, "fallback:7"), nullptr) << output;
    EXPECT_NE(strstr(output, "coercion:0:undefined"), nullptr) << output;
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_STORE][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_STORE][2], 0u);

    char* mir = read_fixture_mir("typed_array_store_leaf");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "js_typed_array_set_number_if_kind"), nullptr);
    EXPECT_NE(strstr(mir, "js_typed_array_data_at_if_kind"), nullptr);
    // The signed Int32Array read is a physical i32 load before its F64 use.
    EXPECT_NE(strstr(mir, "i32:("), nullptr);
    free(mir);
    expect_trace_off_same("typed_array_store_leaf", source, output);
}

TEST(JsOpt, TypedArrayParameterUsesGuardedPhysicalAccess) {
    const char* source =
        "function transform(data, limit) {\n"
        "  let total = 0;\n"
        "  for (let i = 0; i < limit; i++) {\n"
        "    data[i] = data[i] + 0.5;\n"
        "    total += data[i];\n"
        "  }\n"
        "  return total;\n"
        "}\n"
        "function multiply(data, limit) {\n"
        "  let scale = 2;\n"
        "  let total = 0;\n"
        "  for (let i = 0; i < limit; i++) total += scale * data[i];\n"
        "  return total;\n"
        "}\n"
        "function int32Transform(data, limit) {\n"
        "  let total = 0;\n"
        "  for (let i = 0; i < limit; i++) { data[i] = data[i] + 1; total += data[i]; }\n"
        "  return total;\n"
        "}\n"
        "let data = new Float64Array(4);\n"
        "for (let i = 0; i < 4; i++) data[i] = i;\n"
        "console.log('sum:' + transform(data, 4));\n"
        "console.log('product:' + multiply(data, 4));\n"
        "let ints = new Int32Array(3); ints[0] = -2; ints[1] = 2147483647; ints[2] = -1;\n"
        "console.log('int32:' + int32Transform(ints, 3) + ':' + ints[0] + ':' + ints[1] + ':' + ints[2]);\n"
        "let indirectMultiply = multiply;\n"
        "console.log('wrong-kind:' + indirectMultiply([2], 1));\n"
        "data = [3];\n"
        "data[0] += 2;\n"
        "console.log('fallback:' + data[0]);\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("typed_array_parameter_access", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_NE(strstr(output, "sum:8"), nullptr) << output;
    EXPECT_NE(strstr(output, "product:16"), nullptr) << output;
    EXPECT_NE(strstr(output, "int32:-2147483649:-1:-2147483648:0"), nullptr) << output;
    EXPECT_NE(strstr(output, "wrong-kind:4"), nullptr) << output;
    EXPECT_NE(strstr(output, "fallback:5"), nullptr) << output;
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_READ][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_READ][2], 0u);

    char* mir = read_fixture_mir("typed_array_parameter_access");
    ASSERT_NE(mir, nullptr);
    // The direct caller nominates Float64Array, but the generated function
    // still guards the runtime receiver before taking its physical load path.
    EXPECT_NE(strstr(mir, "js_typed_array_data_at_if_kind"), nullptr);
    EXPECT_NE(strstr(mir, "dmul"), nullptr);
    EXPECT_NE(strstr(mir, "i32:("), nullptr);
    free(mir);
    expect_trace_off_same("typed_array_parameter_access", source, output);
}

TEST(JsOpt, NestedTypedArrayArithmeticRetainsNumberResultAfterGenericMiss) {
    const char* source =
        "function transform(data) {\n"
        "  let scale = 5;\n"
        "  let product = scale * data[0] - scale * data[1];\n"
        "  data[2] = data[0] - product;\n"
        "  return product + ':' + data[2];\n"
        "}\n"
        "let typed = new Float64Array(3); typed[0] = 2; typed[1] = 3;\n"
        "console.log('typed:' + transform(typed));\n"
        "let indirect = transform;\n"
        "console.log('array:' + indirect([2, 3, 0]));\n"
        "let coercions = 0;\n"
        "let objects = [{ valueOf() { coercions++; return 2; } },\n"
        "               { valueOf() { coercions++; return 3; } }, 0];\n"
        "console.log('object:' + indirect(objects) + ':' + coercions);\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("nested_typed_array_number_result", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_NE(strstr(output, "typed:-5:7"), nullptr) << output;
    EXPECT_NE(strstr(output, "array:-5:7"), nullptr) << output;
    EXPECT_NE(strstr(output, "object:-5:7:3"), nullptr) << output;
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_READ][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_READ][2], 0u);

    char* mir = read_fixture_mir("nested_typed_array_number_result");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "dmul"), nullptr);
    EXPECT_NE(strstr(mir, "dsub"), nullptr);
    EXPECT_NE(strstr(mir, "js_typed_array_set_number_if_kind"), nullptr);
    free(mir);
    expect_trace_off_same("nested_typed_array_number_result", source, output);
}

TEST(JsOpt, LeftTypedArrayArithmeticRetainsNumberResultAfterGenericMiss) {
    const char* source =
        "function transform(data) {\n"
        "  let scale = 5;\n"
        "  let product = data[0] * scale - data[1] * scale;\n"
        "  data[2] = data[0] - product;\n"
        "  return product + ':' + data[2];\n"
        "}\n"
        "let typed = new Float64Array(3); typed[0] = 2; typed[1] = 3;\n"
        "console.log('typed:' + transform(typed));\n"
        "let indirect = transform;\n"
        "console.log('array:' + indirect([2, 3, 0]));\n"
        "let coercions = 0;\n"
        "let objects = [{ valueOf() { coercions++; return 2; } },\n"
        "               { valueOf() { coercions++; return 3; } }, 0];\n"
        "console.log('object:' + indirect(objects) + ':' + coercions);\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("left_typed_array_number_result", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_NE(strstr(output, "typed:-5:7"), nullptr) << output;
    EXPECT_NE(strstr(output, "array:-5:7"), nullptr) << output;
    EXPECT_NE(strstr(output, "object:-5:7:3"), nullptr) << output;
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_READ][1], 0u);
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_READ][2], 0u);

    char* mir = read_fixture_mir("left_typed_array_number_result");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "dmul"), nullptr);
    EXPECT_NE(strstr(mir, "dsub"), nullptr);
    EXPECT_NE(strstr(mir, "js_typed_array_set_number_if_kind"), nullptr);
    free(mir);
    expect_trace_off_same("left_typed_array_number_result", source, output);
}

TEST(JsOpt, TypedArrayKindFollowsStableDirectParameterForwarding) {
    const char* source =
        "function leaf(data) { return data[0]; }\n"
        "function forward(data, count) {\n"
        "  return count ? forward(data, count - 1) : leaf(data);\n"
        "}\n"
        "let values = new Int32Array(1); values[0] = -3;\n"
        "if (forward(values, 1) !== -3) throw new Error('forwarded Int32Array changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("typed_array_parameter_forwarding", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);
    EXPECT_GT(trace.events[JS_OPT_TYPED_NUMBER_READ][1], 0u);

    char* mir = read_fixture_mir("typed_array_parameter_forwarding");
    ASSERT_NE(mir, nullptr);
    const char* leaf_end = NULL;
    const char* leaf = find_mir_function(mir, "_js_leaf_", &leaf_end);
    ASSERT_NE(leaf, nullptr);
    ASSERT_NE(leaf_end, nullptr);
    // The concrete caller nominates Int32Array through one forwarding frame;
    // its self-call is cyclic evidence and cannot override that concrete kind.
    // The leaf still retains its runtime receiver guard before its signed load.
    const char* typed_data = strstr(leaf, "js_typed_array_data_at_if_kind");
    EXPECT_TRUE(typed_data && typed_data < leaf_end);
    const char* signed_load = strstr(leaf, "i32:(");
    EXPECT_TRUE(signed_load && signed_load < leaf_end);
    free(mir);
    expect_trace_off_same("typed_array_parameter_forwarding", source, output);
}

TEST(JsOpt, TypedArrayKindRejectsConflictingDirectCallers) {
    const char* source =
        "function leaf(data) { return data[0]; }\n"
        "let signed = new Int32Array(1); signed[0] = -3;\n"
        "let unsigned = new Uint8Array(1); unsigned[0] = 5;\n"
        "if (leaf(signed) !== -3 || leaf(unsigned) !== 5)\n"
        "  throw new Error('mixed typed callers changed');\n"
        "console.log('OPT_OK');\n";
    TraceResult trace;
    char output[4096];
    ASSERT_TRUE(run_fixture("typed_array_parameter_conflict", source, &trace,
        output, sizeof(output)));
    expect_ok_output(output);

    char* mir = read_fixture_mir("typed_array_parameter_conflict");
    ASSERT_NE(mir, nullptr);
    const char* leaf_end = NULL;
    const char* leaf = find_mir_function(mir, "_js_leaf_", &leaf_end);
    ASSERT_NE(leaf, nullptr);
    ASSERT_NE(leaf_end, nullptr);
    // A function with competing direct brands must retain the generic path;
    // selecting either physical layout would make its other caller unsound.
    const char* typed_data = strstr(leaf, "js_typed_array_data_at_if_kind");
    EXPECT_FALSE(typed_data && typed_data < leaf_end);
    free(mir);
    expect_trace_off_same("typed_array_parameter_conflict", source, output);
}

TEST(JsOpt, MirLogicalJoinPublishesMergedCarrier) {
    const char* source =
        "function logicalJoinCarrier(e) {\n"
        "  try { return (null == e._pf && (e._pf = {ready: true}), e._pf); }\n"
        "  catch (error) { return 'threw'; }\n"
        "}\n"
        "var state = {_pf: {ready: true}};\n"
        "var throwing = {}; Object.defineProperty(throwing, '_pf', {get: function() { throw new Error('expected'); }});\n"
        "if (!logicalJoinCarrier(state).ready || logicalJoinCarrier(throwing) !== 'threw') throw new Error('bad join');\n"
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
    // D8.4.3: a short-circuit RHS value is undefined on the other edge. The
    // normal join must use one carrier, whether completion is checked there
    // or each fallible arm has already routed before reaching the join.
    EXPECT_TRUE(mir_branch_join_has_defined_carrier(
        mir, "_js_logicalJoinCarrier_", "js_equal,", "bt"));
    free(mir);
    expect_trace_off_same("mir_logical_join_carrier", source, output);
}

TEST(JsOpt, MirConditionalJoinPublishesMergedCarrier) {
    const char* source =
        "function conditionalJoinCarrier(flag, state) {\n"
        "  'use strict';\n"
        "  try { return ((flag ? (state.x = {ready: true}) : state.x), state.x); }\n"
        "  catch (error) { return 'threw'; }\n"
        "}\n"
        "var state = {x: {ready: true}};\n"
        "var throwingGet = {}; Object.defineProperty(throwingGet, 'x', {get: function() { throw new Error('expected'); }});\n"
        "var throwingSet = {}; Object.defineProperty(throwingSet, 'x', {value: 0, writable: false});\n"
        "if (!conditionalJoinCarrier(false, state).ready || conditionalJoinCarrier(false, throwingGet) !== 'threw' || conditionalJoinCarrier(true, throwingSet) !== 'threw') throw new Error('bad join');\n"
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
    // D8.4.3: both conditional arms publish one result, while the throwing
    // getter/setter calls above exercise the arm-local completion routes.
    EXPECT_TRUE(mir_branch_join_has_defined_carrier(
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
