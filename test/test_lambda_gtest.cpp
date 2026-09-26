//==============================================================================
// Lambda Script Tests - Auto-Discovery Based (MIR Direct path)
//
// This file auto-discovers and tests Lambda scripts against expected outputs
// using the MIR Direct transpilation path (default JIT).
//==============================================================================

#include "test_lambda_helpers.hpp"
#include "test_ast_tune_capture.hpp"
#include "../lib/shell.h"
#include <string.h>

//==============================================================================
// Directory Configuration for Baseline Tests
//==============================================================================

// Functional scripts (executed with ./lambda.exe <script>)
static const char* FUNCTIONAL_TEST_DIRECTORIES[] = {
    "test/lambda",
    "test/lambda/chart",
    "test/lambda/latex",
    "test/lambda/math",
    "test/lambda/editor",
    "test/lambda/editing",
    "test/lambda/graph/mermaid",
    "test/lambda/graph/graphviz",
    "test/lambda/graph/structurizr",
    // Add more functional test directories here as needed
};
static const size_t NUM_FUNCTIONAL_TEST_DIRECTORIES = sizeof(FUNCTIONAL_TEST_DIRECTORIES) / sizeof(FUNCTIONAL_TEST_DIRECTORIES[0]);

// Procedural scripts (executed with ./lambda.exe run <script>)
static const char* PROCEDURAL_TEST_DIRECTORIES[] = {
    "test/lambda/proc",
    "test/lambda/conc",
    "test/lambda/pdf",
    "test/benchmark/awfy",
    "test/benchmark/r7rs",
    "test/benchmark/beng",
    "test/benchmark/kostya",
    "test/benchmark/larceny",
    // Add more procedural test directories here as needed
};
static const size_t NUM_PROCEDURAL_TEST_DIRECTORIES = sizeof(PROCEDURAL_TEST_DIRECTORIES) / sizeof(PROCEDURAL_TEST_DIRECTORIES[0]);

//==============================================================================
// Test Discovery
//==============================================================================

// Discover all tests from all configured directories
std::vector<LambdaTestInfo> discover_all_tests() {
    std::vector<LambdaTestInfo> all_tests;

    // Discover functional script tests
    for (size_t i = 0; i < NUM_FUNCTIONAL_TEST_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(FUNCTIONAL_TEST_DIRECTORIES[i], false);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }

    // Discover procedural script tests
    for (size_t i = 0; i < NUM_PROCEDURAL_TEST_DIRECTORIES; i++) {
        std::vector<LambdaTestInfo> dir_tests = discover_tests_in_directory(PROCEDURAL_TEST_DIRECTORIES[i], true);
        all_tests.insert(all_tests.end(), dir_tests.begin(), dir_tests.end());
    }

    // Filter out slow benchmark tests. The MIR skip list was retired on
    // 2026-09-25, once every entry matched its golden on interp, jit and auto:
    // the object tests after LR07-23, object_direct_access after its `open`
    // field was renamed, and beng_fasta once its seed became a `var` parameter.
    std::vector<LambdaTestInfo> filtered;
    for (const auto& test : all_tests) {
        if (is_slow_benchmark(test.test_name)) continue;
        filtered.push_back(test);
    }
    return filtered;
}

// Global test list (populated before main)
static std::vector<LambdaTestInfo> g_lambda_tests;

// GTest filters match the full parameterized test name:
// AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/<test_name>
static bool lambda_filter_wildcard_match(const char* pattern, const char* text) {
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return true;
            while (*text) {
                if (lambda_filter_wildcard_match(pattern, text)) return true;
                text++;
            }
            return lambda_filter_wildcard_match(pattern, text);
        }
        if (*pattern == '?') {
            if (*text == '\0') return false;
            pattern++;
            text++;
            continue;
        }
        if (*pattern != *text) return false;
        pattern++;
        text++;
    }
    return *text == '\0';
}

static bool lambda_filter_pattern_list_matches(
    const char* patterns, const char* patterns_end, const char* full_name)
{
    const char* pat = patterns;
    while (pat < patterns_end) {
        const char* pat_end = pat;
        while (pat_end < patterns_end && *pat_end != ':') pat_end++;

        if (pat_end > pat) {
            char pattern[512];
            size_t len = (size_t)(pat_end - pat);
            if (len >= sizeof(pattern)) len = sizeof(pattern) - 1;
            memcpy(pattern, pat, len);
            pattern[len] = '\0';
            if (lambda_filter_wildcard_match(pattern, full_name)) return true;
        }

        pat = pat_end + 1;
    }
    return false;
}

static bool lambda_script_matches_gtest_filter(const LambdaTestInfo& test, const char* filter) {
    if (!filter || filter[0] == '\0') filter = "*";

    char full_name[512];
    snprintf(full_name, sizeof(full_name),
             "AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/%s",
             test.test_name.c_str());

    const char* negative_patterns = strchr(filter, '-');
    const char* positive_end = negative_patterns ? negative_patterns : filter + strlen(filter);
    bool positive_match = positive_end == filter ||
        lambda_filter_pattern_list_matches(filter, positive_end, full_name);
    if (!positive_match) return false;

    if (negative_patterns) {
        const char* negative_start = negative_patterns + 1;
        const char* negative_end = filter + strlen(filter);
        if (lambda_filter_pattern_list_matches(negative_start, negative_end, full_name)) return false;
    }
    return true;
}

//==============================================================================
// Parameterized Test Class for Lambda Scripts (Batch Mode)
//==============================================================================

class LambdaScriptTest : public ::testing::TestWithParam<LambdaTestInfo> {
public:
    static std::unordered_map<std::string, BatchResult> batch_results;
    static bool batch_executed;

    static void SetUpTestSuite() {
        if (batch_executed) return;

        // keep the full gtest filter; truncating long colon-separated filters drops scripts from the batch.
        std::string gtest_filter = ::testing::GTEST_FLAG(filter);
        std::vector<std::string> scripts;
        std::vector<bool> procs;
        for (const auto& test : g_lambda_tests) {
            if (!lambda_script_matches_gtest_filter(test, gtest_filter.c_str())) continue;
            scripts.push_back(test.script_path);
            procs.push_back(test.is_procedural);
        }

        batch_results = execute_lambda_batch(scripts, procs,
                                             lambda_capture_batch_chunk_size());
        batch_executed = true;
    }
};

std::unordered_map<std::string, BatchResult> LambdaScriptTest::batch_results;
bool LambdaScriptTest::batch_executed = false;

TEST_P(LambdaScriptTest, ExecuteAndCompare) {
    const LambdaTestInfo& info = GetParam();

    // Look up batch result
    auto it = batch_results.find(info.script_path);
    ASSERT_TRUE(it != batch_results.end())
        << "Script not found in batch results: " << info.script_path;

    const BatchResult& br = it->second;
    char elapsed_us_buf[64];
    snprintf(elapsed_us_buf, sizeof(elapsed_us_buf), "%lld", br.elapsed_us);
    RecordProperty("lambda_script_elapsed_us", elapsed_us_buf);
    char elapsed_ms_buf[64];
    snprintf(elapsed_ms_buf, sizeof(elapsed_ms_buf), "%.3f", (double)br.elapsed_us / 1000.0);
    RecordProperty("lambda_script_elapsed_ms", elapsed_ms_buf);
    ast_tune_append_timing_row("lambda", info.script_path.c_str(),
        info.test_name.c_str(), br.status, &br.timing, br.has_timing,
        br.has_volume);

    ASSERT_EQ(br.status, 0) << "Script execution failed: " << info.script_path;

    // Extract output (handle ##### Script marker)
    char* actual_output = extract_script_output(br.output);
    ASSERT_NE(actual_output, nullptr) << "Could not extract output for: " << info.script_path;

    trim_trailing_whitespace(actual_output);
    strip_timing_lines(actual_output);
    trim_trailing_whitespace(actual_output);

    // Read expected output
    char* expected_output = read_expected_output(info.expected_path.c_str());
    ASSERT_NE(expected_output, nullptr) << "Could not read expected file: " << info.expected_path;

    ASSERT_STREQ(expected_output, actual_output)
        << "Output mismatch for script: " << info.script_path
        << " (expected " << strlen(expected_output) << " chars, got " << strlen(actual_output) << " chars)";

    free(expected_output);
    free(actual_output);
}

// Custom name generator for better test output
std::string LambdaTestNameGenerator(const ::testing::TestParamInfo<LambdaTestInfo>& info) {
    return info.param.test_name;
}

// This will be populated in main() before RUN_ALL_TESTS()
INSTANTIATE_TEST_SUITE_P(
    AutoDiscovered,
    LambdaScriptTest,
    ::testing::ValuesIn(g_lambda_tests),
    LambdaTestNameGenerator
);

//==============================================================================
// Negative Tests - verify transpiler reports errors gracefully without crashing
//==============================================================================

TEST(LambdaTypedPathTests, PreservesSnapshotsAndRejectsInvalidWrites) {
    test_lambda_script_against_file("test/mir/lambda/typed_path_store.ls",
        "test/mir/lambda/typed_path_store.txt", true);
}

// The default auto tier interprets this cold main, so pin the JIT as well: its
// typed index guard dereferenced a null `rows[1].values` receiver (SIGSEGV)
// while auto and interp printed the golden.
TEST(LambdaTypedPathTests, PreservesSnapshotsAndRejectsInvalidWritesJit) {
    test_lambda_script_against_file("test/mir/lambda/typed_path_store.ls",
        "test/mir/lambda/typed_path_store.txt", true, "jit");
}

TEST(LambdaTypedPathTests, ReopensBoxedTypedAdapterResultForNativeConsumer) {
    test_lambda_script_against_file(
        "test/mir/lambda/result47_untyped_native_adapter.ls",
        "test/mir/lambda/result47_untyped_native_adapter.txt", true, "jit");
}

// Tune27 (§10.15): the baseline runs the auto tier, where a once-called body
// stays in T0 and a JIT-only defect hides behind a T0 golden -- the nullable
// float store that stored its null sentinel as an element was found only by
// running every goldened fixture on every tier. These pin the rounds' fixtures
// on interp, jit and auto (S9.1.2, S9.1.3, S7.1.3v2, D8.1.1v10).
struct TierParityFixture {
    const char* script;
    const char* expected;
};
static const TierParityFixture kTune27TierParity[] = {
    {"test/lambda/proc/cow_var_typed_rebind.ls", "test/lambda/proc/cow_var_typed_rebind.txt"},
    {"test/lambda/proc/cow_var_nullable_record.ls", "test/lambda/proc/cow_var_nullable_record.txt"},
    {"test/lambda/proc/cow_var_nullable_record_typed_handle.ls",
     "test/lambda/proc/cow_var_nullable_record_typed_handle.txt"},
    {"test/lambda/proc/cow_place_mutator.ls", "test/lambda/proc/cow_place_mutator.txt"},
    {"test/lambda/proc/cow_rmw_sibling_borrow.ls", "test/lambda/proc/cow_rmw_sibling_borrow.txt"},
    {"test/lambda/proc/cow_move_out_bind.ls", "test/lambda/proc/cow_move_out_bind.txt"},
    // Tune28: D4.4.6 place-copy marks and CW36 branch store-backs
    {"test/lambda/proc/cow_place_copy_place_written.ls",
     "test/lambda/proc/cow_place_copy_place_written.txt"},
    {"test/lambda/proc/cow_rmw_branch_store_back.ls",
     "test/lambda/proc/cow_rmw_branch_store_back.txt"},
    {"test/lambda/proc/tune27_nullable_lane_store.ls", "test/lambda/proc/tune27_nullable_lane_store.txt"},
    {"test/lambda/proc/tune27_fixed_path_store.ls", "test/lambda/proc/tune27_fixed_path_store.txt"},
    {"test/lambda/proc/tune27_loop_accumulator.ls", "test/lambda/proc/tune27_loop_accumulator.txt"},
    {"test/lambda/proc/tune27_int_sentinel_arith.ls", "test/lambda/proc/tune27_int_sentinel_arith.txt"},
    {"test/lambda/proc/interp_typed_var_rebind.ls", "test/lambda/proc/interp_typed_var_rebind.txt"},
    {"test/mir/lambda/tune26_nullable_float_store.ls", "test/mir/lambda/tune26_nullable_float_store.txt"},
    {"test/mir/lambda/tune27_contract_reuse.ls", "test/mir/lambda/tune27_contract_reuse.txt"},
    {"test/mir/lambda/tune27_literal_extent_dense.ls", "test/mir/lambda/tune27_literal_extent_dense.txt"},
    {"test/mir/lambda/tune27_dense_store_guard.ls", "test/mir/lambda/tune27_dense_store_guard.txt"},
    {"test/mir/lambda/tune27_place_borrow.ls", "test/mir/lambda/tune27_place_borrow.txt"},
    {"test/mir/lambda/tune27_call_defined_binding.ls", "test/mir/lambda/tune27_call_defined_binding.txt"},
    {"test/mir/lambda/tune27_float_literal_nullable.ls", "test/mir/lambda/tune27_float_literal_nullable.txt"},
    // List fixes P4 (S12.3.5v2): the retired `item_spread` marked its operand,
    // and on the JIT that operand could be a pooled constant literal -- the
    // same function then returned a list on every later call, where the
    // interpreter returned an array. The divergence is only visible on a tier
    // that compiles, so this fixture is pinned to all three.
    {"test/lambda/spread_star.ls", "test/lambda/spread_star.txt"},
    // S11.4.1v3 (J1/J2): the JIT alone skipped deferred boundary checks -- a
    // literal read as `null`, and concrete values bound to `T[]?`, `T*`, `N?`
    // and reassigned `var`s -- while T0 raised E201. Only a compiling tier
    // can show it, so the fixture is pinned to all three.
    {"test/lambda/proc/boundary_deferred_checks.ls",
     "test/lambda/proc/boundary_deferred_checks.txt"},
    // S11.1.1v3/S11.1.6v2: admitted `T?[]` native lanes read by every consumer,
    // nullable element contracts, and rank. The JIT keeps its own lane reads
    // and call-result unboxing, so these are pinned to all three tiers.
    {"test/lambda/type_nullable_array.ls", "test/lambda/type_nullable_array.txt"},
    {"test/lambda/type_array_rank.ls", "test/lambda/type_array_rank.txt"},
    // S11.1.1v3: counted ranks (`int[2][3]`) parse and count each axis.
    {"test/lambda/type_counted_rank.ls", "test/lambda/type_counted_rank.txt"},
    {"test/lambda/proc/native_lane_consumers.ls",
     "test/lambda/proc/native_lane_consumers.txt"},
    // D3.2.4v4: a reordered literal reifies into its contract's layout by
    // name. The JIT adopted the contract and filled it in source order, so
    // both tiers misread it, and only a compiling tier shows the first half.
    {"test/lambda/proc/map_contract_reordered_literal.ls",
     "test/lambda/proc/map_contract_reordered_literal.txt"},
    // S11.1.6v2 + S11.4.5: a `float?` / `float | null` contract admits an int
    // as `float` does. The JIT failed MIR verification on `let x: float? = 5`
    // and T0 kept the int, so only all three tiers together show both.
    {"test/lambda/proc/nullable_float_lane_admission.ls",
     "test/lambda/proc/nullable_float_lane_admission.txt"},
    // S7.1.1v3/S7.10.5v3: null through the numeric functions and unary
    // operators. The JIT's native libm, rounding and unary lanes computed on
    // the null sentinel's bits, which only a compiling tier shows.
    {"test/lambda/proc/null_numeric_propagation.ls",
     "test/lambda/proc/null_numeric_propagation.txt"},
    // S11.1.1v3: views and N-D arrays cross contracts with no exact packed
    // lane on both tiers, and the JIT's `len` of an annotated N-D binding
    // read the cached leaf count, which only a compiling tier shows.
    {"test/lambda/proc/array_view_admission.ls",
     "test/lambda/proc/array_view_admission.txt"},
    // S11.1.6v2/D2.5.1: `bool?`/`string?` call results and null pointer lanes
    // bound by `let`/`var`. The JIT stored a call's raw lane as an Item and
    // re-tagged a binding's ItemNull, which only a compiling tier shows.
    {"test/lambda/proc/nullable_lane_bindings.ls",
     "test/lambda/proc/nullable_lane_bindings.txt"},
    // S11.1.5v2: a call through a function-type contract yields the signature's
    // return type, curried calls included. The JIT unboxes such a call result
    // by that type and a map literal lays out its field by it, so a result
    // typed as the wrong kind segfaulted on both tiers.
    {"test/lambda/fn_type_curried_call.ls", "test/lambda/fn_type_curried_call.txt"},
    // Typed Array 4 Scope 3: a view reads and writes through its strides. A
    // row of a transposed matrix read and wrote its base's next elements on
    // both tiers; the JIT's own fast paths must keep bailing for views.
    {"test/lambda/proc/array_view_strides.ls",
     "test/lambda/proc/array_view_strides.txt"},
    // S7.9.3/S1.6: a `bool` system function's error result stays an error.
    // The JIT narrowed any/all through truthiness (`false`) and branched on
    // the BOOL_ERROR of contains/starts_with as truth; interp kept the Item.
    {"test/lambda/proc/sysfunc_bool_error_lane.ls",
     "test/lambda/proc/sysfunc_bool_error_lane.txt"},
    // S4.1.1/S4.2.2 (LR04-9): int() keeps in-band values exact and passes
    // inf/nan through. T0 wrapped parsed text to int32, returned in-band
    // floats beyond int32 as floats, and read int(nan) as 0.
    {"test/lambda/int_conversion_band.ls", "test/lambda/int_conversion_band.txt"},
    // S7.4.4 (LR10-7): an error value owns its code and message. Both tiers
    // read context->last_error, so every error reported the last one built.
    {"test/lambda/error_value_payload.ls", "test/lambda/error_value_payload.txt"},
    // S1.6 (LR07-17): the JIT's const fold read an imported literal's span in
    // the importer's own source, so `pub let A = 10` imported as 0. The
    // baseline runs goldens on `auto` only, which hid it in import_vars too.
    {"test/lambda/import_const_exports.ls", "test/lambda/import_const_exports.txt"},
    {"test/lambda/import_vars.ls", "test/lambda/import_vars.txt"},
    // S11.4.5 (LR03-11): in-range sized admission and wrapping conversions.
    {"test/lambda/sized_admission_values.ls", "test/lambda/sized_admission_values.txt"},
    // S11.2.1 (LR03-11): literal types are singletons in `is` and admission.
    {"test/lambda/type_literal_admission.ls", "test/lambda/type_literal_admission.txt"},
    // S9.1.1 (LR12-27): push onto an open numeric array appends, keeping the
    // lane or widening; it had been a silent no-op on every tier.
    {"test/lambda/proc/push_open_packed.ls", "test/lambda/proc/push_open_packed.txt"},
    // S7.10.2/S11.4.9/S1.6 (LR07-18): rows that return an error declare it, so
    // the JIT keeps the error instead of unboxing it into "<error>", nan or 0.
    {"test/lambda/proc/sysfunc_text_error_lane.ls",
     "test/lambda/proc/sysfunc_text_error_lane.txt"},
    // The same defect was already pinned here, hidden because the baseline
    // runs goldens on `auto`, which starts in T0.
    {"test/lambda/slice_float_indices.ls", "test/lambda/slice_float_indices.txt"},
    // S3.1 (LR07-21): a for-in over bool elements bound the fetched Item in
    // bool's 0/1 lane, so the JIT's `if`, `not` and `where` read `false` as
    // true; `auto` starts in T0 and hid it.
    {"test/lambda/proc/for_in_bool_truthiness.ls",
     "test/lambda/proc/for_in_bool_truthiness.txt"},
    // S1.6 (LR07-23..27): goldens that failed only on the JIT, found by running
    // every golden with LAMBDA_TIER=jit. A method's `_b` wrapper re-boxed its
    // Item result (six segfaults), a widened bool array's slow read folded "x"
    // to false (D3.3.1v2), a repeated literal key read its first entry, a
    // string-pattern `case` compared with `==`, and a direct store wrote a raw
    // int64 over an `i64?` field's TypedItem.
    {"test/lambda/object_method_receiver.ls", "test/lambda/object_method_receiver.txt"},
    {"test/lambda/proc/proc_fill_bool_lane.ls", "test/lambda/proc/proc_fill_bool_lane.txt"},
    {"test/lambda/map_duplicate_key_lookup.ls", "test/lambda/map_duplicate_key_lookup.txt"},
    {"test/lambda/match_string_pattern.ls", "test/lambda/match_string_pattern.txt"},
    {"test/lambda/proc/proc_nullable_native_i64_map.ls",
     "test/lambda/proc/proc_nullable_native_i64_map.txt"},
    // S11.1.3 (LR03-18, LR03-14): a range type admits its members inside a
    // union, map or array type and at a parameter, on every tier (D3.1.1v4).
    {"test/lambda/range_type_membership.ls", "test/lambda/range_type_membership.txt"},
    // S11.4.10 (LR03-20): an object literal admits each field against its
    // declared contract; construction stored any value unchecked.
    {"test/lambda/object_field_admission.ls", "test/lambda/object_field_admission.txt"},
    // S8.2.4v3 (LR07-31..35): type keys step lists and drop null matches,
    // positional selections gather; a run-time-typed key read element 0 on the
    // JIT only, and `last` there resolved against an outer container.
    {"test/lambda/subscript_selection.ls", "test/lambda/subscript_selection.txt"},
    {"test/lambda/proc/subscript_last_scope.ls", "test/lambda/proc/subscript_last_scope.txt"},
};

TEST(LambdaTierParityTests, Tune27FixturesAgreeOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t f = 0; f < sizeof(kTune27TierParity) / sizeof(kTune27TierParity[0]); f++) {
        for (size_t t = 0; t < 3; t++) {
            SCOPED_TRACE(std::string(kTune27TierParity[f].script) + " on " + tiers[t]);
            test_lambda_script_against_file(kTune27TierParity[f].script,
                kTune27TierParity[f].expected, true, tiers[t]);
        }
    }
}

// Sets an environment variable for the child scripts of one test and restores
// the previous value when the test leaves scope, even through an assertion.
class ScopedTestEnv {
    const char* name_;
    char* saved_;

public:
    ScopedTestEnv(const char* name, const char* value) : name_(name) {
        const char* previous = shell_getenv(name);
        saved_ = previous ? strdup(previous) : NULL;
        shell_setenv(name, value);
    }
    ~ScopedTestEnv() {
        if (saved_) shell_setenv(name_, saved_);
        else shell_unsetenv(name_);
        free(saved_);
    }
    ScopedTestEnv(const ScopedTestEnv&) = delete;
    ScopedTestEnv& operator=(const ScopedTestEnv&) = delete;
};

// LR01-16: the auto tier hands a function to its satellite whenever a worker
// finishes, so a defect in how satellites are published appears only for some
// timings. LAMBDA_SATELLITE_SYNC pins each hand-off to the promoting call, and
// the two thresholds move that call. Both fixtures read properties through
// several satellites whose key suffixes are placed at publication (D8.5.1v7).
TEST(LambdaTierParityTests, SatellitePublicationKeepsPropertyKeys) {
    static const TierParityFixture fixtures[] = {
        {"test/lambda/satellite_property_keys.ls", "test/lambda/satellite_property_keys.txt"},
        {"test/lambda/gc_shape_any_lane.ls", "test/lambda/gc_shape_any_lane.txt"},
    };
    static const char* const thresholds[] = {"1", "5"};
    ScopedTestEnv sync("LAMBDA_SATELLITE_SYNC", "1");
    for (const char* threshold : thresholds) {
        ScopedTestEnv jit_threshold("LAMBDA_JIT_THRESHOLD", threshold);
        for (const TierParityFixture& fixture : fixtures) {
            char trace[256];
            snprintf(trace, sizeof(trace), "%s at threshold %s", fixture.script, threshold);
            SCOPED_TRACE(trace);
            test_lambda_script_against_file(fixture.script, fixture.expected, false, "auto");
        }
    }
}

// D8.1.1v12: a published satellite lowers copied definitions, but recursive
// calls resolve the source definition. Force publication at two call counts so
// both functional and procedural self calls retain their tail-call identity.
TEST(LambdaTierParityTests, SatellitePublicationKeepsTailCallIdentity) {
    static const char* const thresholds[] = {"1", "5"};
    ScopedTestEnv sync("LAMBDA_SATELLITE_SYNC", "1");
    for (const char* threshold : thresholds) {
        ScopedTestEnv jit_threshold("LAMBDA_JIT_THRESHOLD", threshold);
        SCOPED_TRACE(threshold);
        test_lambda_script_against_file("test/lambda/tail_call.ls",
            "test/lambda/tail_call.txt", false, "auto");
        test_lambda_script_against_file("test/lambda/proc/tail_call_proc.ls",
            "test/lambda/proc/tail_call_proc.txt", true, "auto");
    }
}

// Tune31 T31-1: a nested counter initialized from the compact outer counter
// must retain its native arithmetic in every execution tier (S4.1.1-S4.1.5).
TEST(LambdaTune31Tests, NestedCounterAgreesOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file("test/mir/lambda/tune31_nested_counter.ls",
            "test/mir/lambda/tune31_nested_counter.txt", true, tiers[t]);
    }
}

TEST(LambdaTune31Tests, RecursiveArrayWitnessAgreesOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file("test/mir/lambda/tune31_recursive_array_witness.ls",
            "test/mir/lambda/tune31_recursive_array_witness.txt", true, tiers[t]);
    }
}

// Tune31 Phase II F / D3.3.3v3: the raw witness is an implementation fact,
// so the inferred parameter's output and snapshot semantics must agree across
// interpreter, direct JIT, and automatic tier selection.
TEST(LambdaTune31Tests, InferredFloatStoreAgreesOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file("test/mir/lambda/tune31_inferred_float_store.ls",
            "test/mir/lambda/tune31_inferred_float_store.txt", true, tiers[t]);
    }
}

TEST(LambdaTune31Tests, InferredVarFloatStoreAgreesOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file("test/mir/lambda/tune31_inferred_var_float_store.ls",
            "test/mir/lambda/tune31_inferred_var_float_store.txt", true, tiers[t]);
    }
}

// Tune31 Phase II F / S7.1.3v2: the inferred fast arm must never bypass a
// null widening or an out-of-range error path.
TEST(LambdaTune31Tests, InferredFloatStoreFallbacksAgreeOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file(
            "test/mir/lambda/tune31_inferred_float_store_fallback.ls",
            "test/mir/lambda/tune31_inferred_float_store_fallback.txt", true, tiers[t]);
    }
}

TEST(LambdaTune31Tests, AppendBuilderReturnAgreesOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file("test/mir/lambda/tune31_append_builder_return.ls",
            "test/mir/lambda/tune31_append_builder_return.txt", true, tiers[t]);
    }
}

TEST(LambdaTune31Tests, NestedBuilderReturnAgreesOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file("test/mir/lambda/tune31_nested_builder_return.ls",
            "test/mir/lambda/tune31_nested_builder_return.txt", true, tiers[t]);
    }
}

TEST(LambdaTune31Tests, BoolNullEqualityAgreesOnEveryTier) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file("test/mir/lambda/tune31_bool_null_equality.ls",
            "test/mir/lambda/tune31_bool_null_equality.txt", true, tiers[t]);
    }
}

TEST(LambdaTune31Tests, BoolNullEqualityWriteKeepsGenericPath) {
    static const char* const tiers[] = {"interp", "jit", "auto"};
    for (size_t t = 0; t < 3; t++) {
        SCOPED_TRACE(tiers[t]);
        test_lambda_script_against_file(
            "test/mir/lambda/tune31_bool_null_equality_reassign.ls",
            "test/mir/lambda/tune31_bool_null_equality_reassign.txt", true, tiers[t]);
    }
}

TEST(LambdaTypedPathTests, ReusesFullArrayContractsAcrossCalls) {
    test_lambda_script_against_file("test/mir/lambda/typed_array_reuse.ls",
        "test/mir/lambda/typed_array_reuse.txt", true);
}

TEST(LambdaTypedPathTests, CertifiesMatchingPrimitiveArrayNumCarriers) {
    test_lambda_script_against_file("test/mir/lambda/tune26_primitive_admission.ls",
        "test/mir/lambda/tune26_primitive_admission.txt", true);
}

TEST(LambdaTypedPathTests, ReusesDeclaredBoolArrayProofAcrossDenseLoop) {
    test_lambda_script_against_file("test/mir/lambda/tune26_dense_declared_bool.ls",
        "test/mir/lambda/tune26_dense_declared_bool.txt", true);
}

TEST(LambdaTypedPathTests, PacksNullableFloatLiteralsAndPreservesNullMembers) {
    test_lambda_script_against_file("test/mir/lambda/tune27_float_literal_nullable.ls",
        "test/mir/lambda/tune27_float_literal_nullable.txt", true);
}

TEST(LambdaTypedPathTests, PrunesDeadLayoutReloadsWithoutStaleArrayReads) {
    test_lambda_script_against_file("test/mir/lambda/tune27_layout_reload_liveness.ls",
        "test/mir/lambda/tune27_layout_reload_liveness.txt", true);
}

TEST(LambdaTypedPathTests, StoresThroughModuleConstantSubscriptsInPlace) {
    test_lambda_script_against_file("test/mir/lambda/tune27_module_const_index_store.ls",
        "test/mir/lambda/tune27_module_const_index_store.txt", true);
}

TEST(LambdaTypedPathTests, ReusesFiniteProofAcrossPositiveSubtractionLoop) {
    test_lambda_script_against_file("test/mir/lambda/tune26_finite_sub_loop.ls",
        "test/mir/lambda/tune26_finite_sub_loop.txt", true);
}

TEST(LambdaTypedPathTests, ReusesFiniteProofAcrossDescendingSumLoop) {
    test_lambda_script_against_file("test/mir/lambda/tune26_descending_sum.ls",
        "test/mir/lambda/tune26_descending_sum.txt", true);
}

TEST(LambdaTypedPathTests, PreservesIntegerParityAcrossFiniteAndSentinelLanes) {
    test_lambda_script_against_file("test/mir/lambda/tune26_parity_compare.ls",
        "test/mir/lambda/tune26_parity_compare.txt", true);
}

TEST(LambdaTypedPathTests, PreservesLiteralOrderedComparisonSentinelSemantics) {
    test_lambda_script_against_file("test/mir/lambda/tune26_literal_ordered_compare.ls",
        "test/mir/lambda/tune26_literal_ordered_compare.txt", true);
}

TEST(LambdaTypedPathTests, PreservesDynamicOrderedComparisonSentinelSemantics) {
    test_lambda_script_against_file("test/mir/lambda/tune26_dynamic_ordered_compare.ls",
        "test/mir/lambda/tune26_dynamic_ordered_compare.txt", true);
}

TEST(LambdaTypedPathTests, PreservesDynamicEqualityComparisonSentinelSemantics) {
    test_lambda_script_against_file("test/mir/lambda/tune26_dynamic_equality_compare.ls",
        "test/mir/lambda/tune26_dynamic_equality_compare.txt", true);
}

TEST(LambdaTypedPathTests, ReusesReadonlyArrayProofBesideVarDestination) {
    test_lambda_script_against_file("test/mir/lambda/tune26_var_readonly_peer.ls",
        "test/mir/lambda/tune26_var_readonly_peer.txt", true);
}

TEST(LambdaTypedPathTests, ReusesFiniteProofAcrossBoundedInduction) {
    test_lambda_script_against_file("test/mir/lambda/tune26_bounded_induction.ls",
        "test/mir/lambda/tune26_bounded_induction.txt", true);
}

TEST(LambdaTypedPathTests, PreservesSnapshotsAcrossSameOwnerStoreLoop) {
    test_lambda_script_against_file("test/mir/lambda/tune26_same_owner_store.ls",
        "test/mir/lambda/tune26_same_owner_store.txt", true);
}

TEST(LambdaTypedPathTests, ReusesUniqueVarParameterProofAcrossStoreLoop) {
    test_lambda_script_against_file("test/mir/lambda/tune26_var_param_unique.ls",
        "test/mir/lambda/tune26_var_param_unique.txt", true);
}

TEST(LambdaTypedPathTests, ReusesVarFloatProofAcrossComputedStoreLoop) {
    test_lambda_script_against_file("test/mir/lambda/tune26_var_float_store.ls",
        "test/mir/lambda/tune26_var_float_store.txt", true);
}

TEST(LambdaTypedPathTests, PreservesPlainParameterSnapshotsAcrossComputedStoreLoop) {
    test_lambda_script_against_file("test/mir/lambda/tune26_plain_param_snapshot_loop.ls",
        "test/mir/lambda/tune26_plain_param_snapshot_loop.txt", true);
}

TEST(LambdaTypedPathTests, LazilySnapshotsPlainBoolArrayParameter) {
    test_lambda_script_against_file("test/mir/lambda/tune26_plain_bool_param_lazy_snapshot.ls",
        "test/mir/lambda/tune26_plain_bool_param_lazy_snapshot.txt", true);
}

TEST(LambdaTypedPathTests, ReusesExclusiveTypedVarReborrow) {
    test_lambda_script_against_file("test/mir/lambda/tune26_typed_reborrow.ls",
        "test/mir/lambda/tune26_typed_reborrow.txt", true);
}

TEST(LambdaTune31Tests, VarPathBorrowKeepsSnapshotIsolated) {
    test_lambda_script_against_file("test/mir/lambda/tune31_var_path_borrow.ls",
        "test/mir/lambda/tune31_var_path_borrow.txt", true);
}

TEST(LambdaTune31Tests, VarPathBorrowPreparesAfterBodySideShare) {
    test_lambda_script_against_file("test/mir/lambda/tune31_var_path_reborrow_shared.ls",
        "test/mir/lambda/tune31_var_path_reborrow_shared.txt", true);
}

TEST(LambdaTypedPathTests, PublishesNestedTypedVarArrayDetach) {
    test_lambda_script_against_file("test/mir/lambda/tune26_typed_var_home_chain.ls",
        "test/mir/lambda/tune26_typed_var_home_chain.txt", true);
}

TEST(LambdaTypedPathTests, PreservesCowStateAcrossLoopBackedge) {
    test_lambda_script_against_file("test/mir/lambda/tune26_loop_cow_backedge.ls",
        "test/mir/lambda/tune26_loop_cow_backedge.txt", true);
}

TEST(LambdaTypedPathTests, StoresNullableFloatLaneThroughColdTypedFallback) {
    test_lambda_script_against_file("test/mir/lambda/tune26_nullable_float_store.ls",
        "test/mir/lambda/tune26_nullable_float_store.txt", true);
}

TEST(LambdaTypedPathTests, ReifiesFreshNumericFillAtTypedDeclaration) {
    test_lambda_script_against_file("test/mir/lambda/tune26_fill_contract_reify.ls",
        "test/mir/lambda/tune26_fill_contract_reify.txt", true);
}

// Helper to test that a script reports type errors but doesn't crash
// Note: Lambda currently exits with code 0 even on type errors (errors are reported to stderr)
void test_lambda_script_expects_error(const char* script_path) {
    const char* args[] = {LAMBDA_EXE, "--no-log", script_path, NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    // Negative script paths are argv data and must not pass through shell parsing.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    std::string output;
    if (shell_result.stdout_buf) {
        output.assign(shell_result.stdout_buf, shell_result.stdout_len);
    }
    shell_result_free(&shell_result);

    // Should contain error messages (type_error, [ERR!], or error[E...])
    bool has_error_msg = output.find("type_error") != std::string::npos ||
                         output.find("[ERR!]") != std::string::npos ||
                         output.find("error[E") != std::string::npos;
    EXPECT_TRUE(has_error_msg) << "Expected error messages in output for: " << script_path
                               << "\nOutput was: " << output;

    // Should NOT contain crash indicators
    EXPECT_EQ(output.find("Segmentation fault"), std::string::npos)
        << "Transpiler crashed on: " << script_path;
    EXPECT_EQ(output.find("SIGABRT"), std::string::npos)
        << "Transpiler aborted on: " << script_path;
}

TEST(LambdaNegativeTests, test_func_param_type_errors) {
    test_lambda_script_expects_error("test/lambda/negative/func_param_negative.ls");
}

TEST(LambdaNegativeTests, test_bare_vector_comparison_error) {
    test_lambda_script_expects_error("test/lambda/negative/semantic/bare_vector_comparison.ls");
}

TEST(LambdaNegativeTests, test_lambda_namespace_root_reserved) {
    test_lambda_script_expects_error("test/lambda/negative/semantic/lambda_namespace_root.ls");
}

TEST(LambdaNegativeTests, test_condition_lint_masks) {
    const char* script_path = "test/lambda/negative/semantic/condition_lint_masks.ls";
    const char* args[] = {LAMBDA_EXE, script_path, NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    char output[65536];
    size_t output_len = shell_result.stdout_len;
    if (output_len >= sizeof(output)) output_len = sizeof(output) - 1;
    if (shell_result.stdout_buf && output_len > 0) {
        memcpy(output, shell_result.stdout_buf, output_len);
    }
    output[output_len] = '\0';
    int exit_code = shell_result.exit_code;
    shell_result_free(&shell_result);

    EXPECT_EQ(exit_code, 0) << "Lint probe should not fail:\n" << output;
    EXPECT_TRUE(strstr(output, "lambda_condition_lint") != nullptr)
        << "Expected condition lint warning:\n" << output;
    EXPECT_TRUE(strstr(output, "elementwise comparison used as if condition") != nullptr)
        << "Expected direct mask-condition warning:\n" << output;
    EXPECT_TRUE(strstr(output, "if condition has container type") != nullptr)
        << "Expected container-condition warning:\n" << output;
    EXPECT_TRUE(strstr(output, "elementwise comparison used as where condition") != nullptr)
        << "Expected where mask-condition warning:\n" << output;
    EXPECT_TRUE(strstr(output, "elementwise comparison used as while condition") != nullptr)
        << "Expected while mask-condition warning:\n" << output;
    EXPECT_TRUE(strstr(output, "mask branchcontainer branch") != nullptr)
        << "Expected script output to still be produced:\n" << output;
}

void test_lambda_proc_script_expects_error(const char* script_path) {
    const char* args[] = {LAMBDA_EXE, "--no-log", "run", script_path, NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    char output[8192];
    size_t output_len = shell_result.stdout_len;
    if (output_len >= sizeof(output)) output_len = sizeof(output) - 1;
    if (shell_result.stdout_buf && output_len > 0) {
        memcpy(output, shell_result.stdout_buf, output_len);
    }
    output[output_len] = '\0';
    int exit_code = shell_result.exit_code;
    shell_result_free(&shell_result);
    EXPECT_NE(exit_code, 0) << "Expected failure for: " << script_path
                                         << "\nOutput was: " << output;

    bool has_error_msg = strstr(output, "Error:") != nullptr ||
                         strstr(output, "[ERR!]") != nullptr ||
                         strstr(output, "error[E") != nullptr;
    EXPECT_TRUE(has_error_msg) << "Expected error messages in output for: " << script_path
                               << "\nOutput was: " << output;

    EXPECT_EQ(strstr(output, "Segmentation fault"), nullptr)
        << "Runtime crashed on: " << script_path;
    EXPECT_EQ(strstr(output, "SIGABRT"), nullptr)
        << "Runtime aborted on: " << script_path;
}

TEST(LambdaNegativeTests, test_typed_array_coercion_error) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/typed_array_coercion_error.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_typed_map_write) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_map_write.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_typed_array_write) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_array_write.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_nested_array_write) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_nested_array_write.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_ndim_array_write) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_ndim_array_write.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_mask_array_write) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_mask_array_write.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_dynamic_arity) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_dynamic_arity.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_dynamic_declaration) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_dynamic_declaration.ls");
}

TEST(LambdaNegativeTests, nullable_array_rejects_null_without_widening) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/nullable_array_reject_null.ls");
}

TEST(LambdaNegativeTests, nullable_pointer_array_rejects_null_without_widening) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/nullable_pointer_array_reject_null.ls");
}

TEST(LambdaNegativeTests, nullable_map_rejects_non_lane_value) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/nullable_map_reject_wrong_type.ls");
}

TEST(LambdaNegativeTests, nullable_index_read_rejects_plain_int) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/nullable_index_read_reject_plain_int.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_dynamic_map) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_dynamic_map.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_dynamic_parameter) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_dynamic_parameter.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_static_float_to_int_parameter) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforce_static_float_to_int_parameter.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_dynamic_return) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforcement_dynamic_return.ls");
}

TEST(LambdaNegativeTests, test_type_enforcement_input_schema) {
    test_lambda_proc_script_expects_error("test/lambda/negative/runtime/type_enforce_input_schema.ls");
}

TEST(LambdaBinaryTests, output_writes_decoded_bytes) {
    const char* script_path = "test/lambda/proc/proc_binary_output.ls";
    const char* args[] = {LAMBDA_EXE, "--no-log", "run", script_path, NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    ASSERT_EQ(shell_result.exit_code, 0);
    shell_result_free(&shell_result);

    FILE* file = fopen("./temp/binary_output.bin", "rb");
    ASSERT_NE(file, nullptr);
    unsigned char bytes[5] = {0};
    size_t count = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    const unsigned char expected[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_EQ(count, sizeof(expected));
    EXPECT_EQ(memcmp(bytes, expected, sizeof(expected)), 0);
}

static void patch_lambda_gtest_json_case_times() {
    std::string output = ::testing::GTEST_FLAG(output);
    const char* json_prefix = "json:";
    if (output.compare(0, strlen(json_prefix), json_prefix) != 0) return;

    std::string json_path = output.substr(strlen(json_prefix));
    if (json_path.empty()) return;

    FILE* file = fopen(json_path.c_str(), "rb");
    if (!file) return;
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(file);
        return;
    }

    std::string json;
    json.resize((size_t)file_size);
    size_t read_size = fread(&json[0], 1, (size_t)file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) return;

    const char* elapsed_key = "\"lambda_script_elapsed_us\": \"";
    const char* time_key = "\"time\": \"";
    size_t pos = 0;
    bool changed = false;
    while ((pos = json.find(elapsed_key, pos)) != std::string::npos) {
        size_t value_start = pos + strlen(elapsed_key);
        size_t value_end = json.find('"', value_start);
        if (value_end == std::string::npos) break;

        long long elapsed_us = atoll(json.c_str() + value_start);
        size_t next_pos = value_end;
        if (elapsed_us > 0) {
            size_t time_pos = json.rfind(time_key, pos);
            if (time_pos != std::string::npos) {
                size_t time_value_start = time_pos + strlen(time_key);
                size_t time_value_end = json.find('"', time_value_start);
                if (time_value_end != std::string::npos && time_value_end < pos) {
                    char time_buf[64];
                    snprintf(time_buf, sizeof(time_buf), "%.3fs", (double)elapsed_us / 1000000.0);
                    size_t old_len = time_value_end - time_value_start;
                    json.replace(time_value_start, old_len, time_buf);
                    long diff = (long)strlen(time_buf) - (long)old_len;
                    next_pos = (size_t)((long)next_pos + diff);
                    changed = true;
                }
            }
        }
        pos = next_pos;
    }

    if (!changed) return;
    file = fopen(json_path.c_str(), "wb");
    if (!file) return;
    fwrite(json.c_str(), 1, json.size(), file);
    fclose(file);
}

//==============================================================================
// Main - discovers tests before running
//==============================================================================

int main(int argc, char **argv) {
    // Discover all lambda script tests before initializing Google Test
    g_lambda_tests = discover_all_tests();

    printf("Discovered %zu lambda script tests:\n", g_lambda_tests.size());
    for (const auto& test : g_lambda_tests) {
        printf("  - %s\n", test.test_name.c_str());
    }
    printf("\n");

    ::testing::InitGoogleTest(&argc, argv);

    // In batch mode (no filter), disable logging for speed.
    // In filtered mode, keep logging enabled for debugging.
    std::string gtest_filter = ::testing::GTEST_FLAG(filter);
    if (gtest_filter == "*") {
#ifdef _WIN32
        _putenv_s("LAMBDA_NO_LOG", "1");
#else
        setenv("LAMBDA_NO_LOG", "1", 1);
#endif
    } else {
#ifdef _WIN32
        _putenv_s("LAMBDA_NO_LOG", "");
#else
        unsetenv("LAMBDA_NO_LOG");
#endif
    }

    int result = RUN_ALL_TESTS();
    patch_lambda_gtest_json_case_times();
    return result;
}
