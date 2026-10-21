// test_mir_gc_stress_gtest.cpp
//
// Forced-GC stress sweep over the MIR emission corpus (MT4 of
// vibe/Lambda_Design_MIR_Emission_Test.md).
//
// MT4 deliberately leaves safepoint currency (Stack API #7) to dynamic oracles
// rather than a static pass: re-implementing CFG liveness would duplicate
// em_finalize_semantic_root_write_back, the hardest code in the emitter, and
// rot alongside it. Instead this binary runs each corpus script under
// exact-root rooting with collection forced at every allocation and freed
// memory poisoned. A value that should be rooted but is not becomes
// unreachable, is collected, and its next use fails deterministically.
//
// The oracle is self-baselining: every stressed run must exit successfully and
// produce byte-identical output to the same script run without stress. That
// needs no golden files, so adding a corpus fixture automatically extends this
// gate, and a fixture whose output legitimately changes cannot silently drift
// out of coverage.
// Reviewed scalar-only/NO_GC fixtures in gc_backend_parity.tsv retain native
// and MIR-interpreter output parity; LAMBDA_MIR_GC_FULL=1 stresses them too.
// Lambda emission fixtures share bounded test-batch processes with a fresh
// heap per script; JS and the additional lifetime regressions stay isolated.
//
// This is behavioral stress over the selected corpus on executed paths, not a
// proof over every CFG path. The design says so explicitly; do not read a green
// run as a rooting proof.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "test_mir_check_helpers.hpp"
#include "test_batch_process.hpp"

namespace {

// scripts beyond the emission corpus that are known to exercise rooting:
// the Stage-4C capture/hoist regressions and the side-stack frame regression
// that the hand-written make gc-rooting gates already watch.
static const char* kExtraJsScripts[] = {
    "test/js/regression_side_stack_frame_gc.js",
    "test/js/array_callback_gc_roots.js",
    "test/js/class_field_arrow_nested_this.js",
    "test/js/class_field_decl_capture_nested_new.js",
    "test/js/class_method_capture_hoist.js",
    "test/js/function_decl_callback_hoist.js",
    "test/js/concurrency_lambda_promise.js",
    "test/js/lambda_data_map.js",
    "test/js/tune4_closure_scalar_ownership.js",
    "test/js/js_shared_scalar_analysis.js",
    "test/js/js_shared_construction_plans.js",
    "test/js/js_shared_native_lanes.js",
    "test/js/js_shared_loop_effects.js",
    "test/js/js_callable_definition_sharing.js",
    "test/js/regression_regexp_split_gc_roots.js",
    "test/node/js_shared_search_leaf.js",
};

static const char* kExtraLambdaScripts[] = {
    // parse converts independent symbol options into allocating string homes.
    "test/lambda/parse_option_roots.ls",
    // native physics builders retain frame/event owners across scalar allocations.
    "test/lambda/physics/freefall.ls",
    "test/lambda/physics/contacts.ls",
    "test/lambda/physics/invalid.ls",
    "test/lambda/physics/package.ls",
    "test/lambda/physics/queries.ls",
    // keep the full reflow/A4 matrix in the golden suite; stress its allocating
    // score/font/SVG boundaries with a small score instead (D5.3.3).
    "test/lambda/music/music_gc_roots.ls",
    "test/lambda/music/music_performance.ls",
    "test/lambda/music/music_midi.ls",
    "test/lambda/cartesian_scratch_roots.ls",
    // typed fill/store/record plans retain owners through checked cold arms
    "test/lambda/proc/typed_tuning_boundary_guards.ls",
    "test/lambda/proc/typed_call_argument_layout.ls",
    // COW spine detachment allocates between every retained owner write.
    "test/lambda/proc/cow_alias.ls",
    // A map field carried in the packed `any` lane is the only reference to
    // its container; the shape walk must decode that lane to reach it.
    "test/lambda/gc_shape_any_lane.ls",
    "test/lambda/proc/string_builder_snapshots.ls",
    // a gather allocates a row view or slice per position between its reads,
    // and a partial N-D read allocates a view between its keys
    "test/lambda/subscript_selection.ls",
    // an index write widens a packed array in place to hold containers; the
    // collector must trace it as an Array from then on
    "test/lambda/proc/ndim_row_assign.ls",
    // runtime-grown maps share tree types and grow their data by doubling
    "test/lambda/proc/map_runtime_shape_tree.ls",
    // symbol subscripts and spreads grow through the same tree edges
    "test/lambda/proc/map_symbol_key_tree.ls",
    // maps and elements grow from external parents; their data relocates
    "test/lambda/proc/map_external_parent.ls",
    // a container evaluated once is held across its key's allocations
    "test/lambda/proc/subscript_last_scope.ls",
};

static constexpr size_t kModeCount = 5;
static constexpr size_t kMaxBatchSize = 128;
struct StressBatch;

struct StressScript {
    std::string path;
    mir_check::Language language = mir_check::LANG_LAMBDA;
    bool procedural = false;
    std::string name;
    size_t index = 0;
    bool emission = false;
    bool selected = false;
    char* parity_reason = nullptr;
    StressBatch* batch = nullptr;
    TestBatchResult runs[kModeCount];
};

std::vector<StressScript> collect_scripts() {
    std::vector<StressScript> scripts;

    std::vector<mir_check::Fixture> lambda_fixtures =
        mir_check::discover_fixtures("test/mir/lambda", ".ls");
    for (size_t i = 0; i < lambda_fixtures.size(); i++) {
        StressScript entry;
        entry.path = lambda_fixtures[i].script_path;
        entry.language = mir_check::LANG_LAMBDA;
        entry.procedural = mir_check::script_is_procedural(entry.path);
        entry.name = "lambda_" + lambda_fixtures[i].name;
        entry.emission = true;
        entry.index = scripts.size();
        scripts.push_back(entry);
    }

    std::vector<mir_check::Fixture> js_fixtures =
        mir_check::discover_fixtures("test/mir/js", ".js");
    for (size_t i = 0; i < js_fixtures.size(); i++) {
        StressScript entry;
        entry.path = js_fixtures[i].script_path;
        entry.language = mir_check::LANG_JS;
        entry.name = "js_" + js_fixtures[i].name;
        entry.emission = true;
        entry.index = scripts.size();
        scripts.push_back(entry);
    }

    for (size_t i = 0; i < sizeof(kExtraJsScripts) / sizeof(kExtraJsScripts[0]); i++) {
        std::string path = kExtraJsScripts[i];
        std::string text;
        // a renamed or deleted regression script must not quietly leave the
        // sweep; fail loudly instead by keeping it in the list.
        StressScript entry;
        entry.path = path;
        entry.language = mir_check::LANG_JS;
        entry.name = "regression_" + mir_check::gtest_safe_name(mir_check::basename_of(path));
        entry.index = scripts.size();
        scripts.push_back(entry);
    }
    for (size_t i = 0; i < sizeof(kExtraLambdaScripts) / sizeof(kExtraLambdaScripts[0]); i++) {
        StressScript entry;
        entry.path = kExtraLambdaScripts[i];
        entry.language = mir_check::LANG_LAMBDA;
        entry.procedural = mir_check::script_is_procedural(entry.path);
        entry.name = "regression_" + mir_check::gtest_safe_name(mir_check::basename_of(entry.path));
        entry.index = scripts.size();
        scripts.push_back(entry);
    }
    return scripts;
}

std::vector<StressScript> g_scripts = collect_scripts();

enum StressMode { REFERENCE, JIT_FORCED, RANDOM_FORCED, INTERP_FORCED, INTERP_PARITY };
static const char* kModeNames[kModeCount] = {
    "reference", "jit-forced", "randomized-forced", "interp-forced", "interp-parity"
};

struct StressBatch {
    ArrayList* scripts;
    bool ready[kModeCount];
};

static ArrayList* g_batches = nullptr;
static StrBuf* g_policy_error = nullptr;
static bool g_policy_loaded = false;
static bool g_full_stress = false;
static size_t g_batch_size = 32;
static size_t g_process_count = 0;

static void load_gc_policy() {
    if (g_policy_loaded) return;
    g_policy_loaded = true;
    g_policy_error = strbuf_new();
    FILE* file = fopen("test/mir/gc_backend_parity.tsv", "rb");
    if (!file) {
        strbuf_append_str(g_policy_error, "cannot read test/mir/gc_backend_parity.tsv");
        return;
    }
    StrBuf* text = strbuf_new();
    bool read_ok = strbuf_append_file(text, file);
    fclose(file);
    if (!read_ok) strbuf_append_str(g_policy_error, "cannot read GC classification policy\n");
    TestProcessLines lines;
    test_process_lines_init(&lines, text->str, text->length);
    char line[2048];
    while (test_process_next_line(&lines, line, sizeof(line))) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
        if (!length || line[0] == '#') continue;
        char* reason = strchr(line, '\t');
        if (!reason || !reason[1]) {
            strbuf_append_str(g_policy_error, "GC policy requires a path and nonempty reason\n");
            continue;
        }
        *reason++ = '\0';
        StressScript* match = nullptr;
        for (size_t i = 0; i < g_scripts.size(); i++) {
            if (g_scripts[i].emission && g_scripts[i].language == mir_check::LANG_LAMBDA &&
                    g_scripts[i].path == line) match = &g_scripts[i];
        }
        if (!match || match->parity_reason) {
            strbuf_append_format(g_policy_error, "unknown or duplicate GC policy path: %s\n", line);
            continue;
        }
        match->parity_reason = str_dup(reason, strlen(reason));
    }
    strbuf_free(text);
}

static bool mode_applies(const StressScript& script, StressMode mode) {
    if (mode == REFERENCE) return true;
    // D8.1.3 retires MIR interpretation for LambdaJS.
    if (script.language == mir_check::LANG_JS) return mode == JIT_FORCED || mode == RANDOM_FORCED;
    bool parity = script.parity_reason && !g_full_stress;
    return parity ? mode == INTERP_PARITY : mode != INTERP_PARITY;
}

static void configure_mode(StressMode mode, ShellEnvEntry* env) {
    bool forced = mode == JIT_FORCED || mode == INTERP_FORCED;
    bool random = mode == RANDOM_FORCED;
    const ShellEnvEntry entries[] = {
        {"LAMBDA_EXEC_BACKEND", "jit"}, {"JS_EXEC_BACKEND", "mir"},
        {"LAMBDA_GC_FORCE_EVERY", forced ? "1" : nullptr},
        {"LAMBDA_GC_FORCE_SEED", random ? "1592594996" : nullptr},
        {"LAMBDA_GC_FORCE_ONE_IN", random ? "3" : nullptr},
        {"LAMBDA_GC_POISON_FREED", forced || random ? "1" : nullptr},
        {"LAMBDA_COMPILER_TIMING", nullptr}, {"LAMBDA_GC_STATS", nullptr},
        {nullptr, nullptr}
    };
    memcpy(env, entries, sizeof(entries));
}

static void execute_batch(StressBatch* batch, StressMode mode) {
    batch->ready[mode] = true;
    const char* paths[kMaxBatchSize];
    TestBatchResult results[kMaxBatchSize];
    StrBuf* manifest = strbuf_new();
    for (int i = 0; i < batch->scripts->length; i++) {
        StressScript* script = (StressScript*)batch->scripts->data[i];
        paths[i] = script->path.c_str();
        strbuf_append_format(manifest, "%s%s\n", script->procedural ? "run " : "", paths[i]);
    }
    char manifest_path[160];
    snprintf(manifest_path, sizeof(manifest_path), "temp/mir_check/gc_batch_%d_%zu.txt",
        (int)MIR_CHECK_GETPID(), g_process_count++);
    FILE* file = fopen(manifest_path, "wb");
    bool written = file && fwrite(manifest->str, 1, manifest->length, file) == manifest->length;
    if (file && fclose(file) != 0) written = false;
    strbuf_free(manifest);
    ShellResult process = {};
    process.exit_code = -1;
    StrBuf* error = strbuf_new();
    if (written) {
        ShellEnvEntry env[9];
        configure_mode(mode, env);
        ShellOptions options = {};
        options.env = env;
        options.stdin_path = manifest_path;
        options.merge_stderr = true;
        options.timeout_ms = batch->scripts->length * 65000;
        const char* args[] = {"--timeout=60", nullptr};
        GTEST_LOG_(INFO) << "mir-gc-batch started: scripts=" << batch->scripts->length
                        << " mode=" << kModeNames[mode];
        process = mir_check::run_lambda_command("test-batch", nullptr,
            mode == INTERP_FORCED || mode == INTERP_PARITY, true, args, &options);
        test_batch_decode(process, paths, (size_t)batch->scripts->length, results, error);
        GTEST_LOG_(INFO) << "mir-gc-batch completed: scripts=" << batch->scripts->length
                        << " mode=" << kModeNames[mode] << " exit=" << process.exit_code;
    } else {
        strbuf_append_str(error, "cannot write batch manifest");
    }
    remove(manifest_path);
    for (int i = 0; i < batch->scripts->length; i++) {
        StressScript* script = (StressScript*)batch->scripts->data[i];
        script->runs[mode] = results[i];
        if (!script->runs[mode].output) script->runs[mode].output = strbuf_new();
        if (error->length) {
            script->runs[mode].exit_code = -1;
            strbuf_append_format(script->runs[mode].output, "\nmir-gc-batch: %s; process_exit=%d\n",
                error->str, process.exit_code);
            size_t diagnostic_length = process.stdout_len < 4096 ? process.stdout_len : 4096;
            if (diagnostic_length) strbuf_append_str_n(script->runs[mode].output,
                process.stdout_buf, diagnostic_length);
        }
    }
    strbuf_free(error);
    shell_result_free(&process);
}

static const TestBatchResult& get_run(StressScript& script, StressMode mode) {
    if (script.batch) {
        if (!script.batch->ready[mode]) execute_batch(script.batch, mode);
    } else if (!script.runs[mode].output) {
        ShellEnvEntry env[9];
        configure_mode(mode, env);
        ShellOptions options = {};
        options.env = env;
        options.merge_stderr = true;
        const char* subcommand = script.language == mir_check::LANG_JS
            ? "js" : script.procedural ? "run" : nullptr;
        ShellResult process = mir_check::run_lambda_command(subcommand, script.path.c_str(),
            mode == INTERP_FORCED || mode == INTERP_PARITY, true, nullptr, &options);
        g_process_count++;
        TestBatchResult& result = script.runs[mode];
        result.complete = true;
        result.exit_code = process.timed_out || process.cancelled || process.output_limit_exceeded
            ? -1 : process.exit_code;
        result.output = strbuf_new();
        if (process.stdout_len) strbuf_append_str_n(result.output, process.stdout_buf, process.stdout_len);
        shell_result_free(&process);
    }
    return script.runs[mode];
}

class MirGcStressTest : public ::testing::TestWithParam<StressScript> {
public:
    static void SetUpTestSuite() {
        load_gc_policy();
        ASSERT_EQ(g_policy_error->length, 0u) << g_policy_error->str;
        const char* full = getenv("LAMBDA_MIR_GC_FULL");
        g_full_stress = full && full[0] && strcmp(full, "0") != 0;
        g_batch_size = 32;
        const char* batch_size = getenv("LAMBDA_MIR_GC_BATCH_SIZE");
        if (batch_size) {
            char* end = nullptr;
            errno = 0;
            unsigned long parsed = strtoul(batch_size, &end, 10);
            ASSERT_TRUE(!errno && end != batch_size && !*end && parsed >= 1 && parsed <= kMaxBatchSize)
                << "LAMBDA_MIR_GC_BATCH_SIZE must be 1.." << kMaxBatchSize;
            g_batch_size = (size_t)parsed;
        }
        g_process_count = 0;
        g_batches = arraylist_new(16);
        const auto* suite = ::testing::UnitTest::GetInstance()->current_test_suite();
        ASSERT_NE(suite, nullptr);
        // should_run() already accounts for filters and GTest sharding; never
        // execute excluded cases just because their neighbors share a batch.
        for (int i = 0; i < suite->total_test_count(); i++) {
            const auto* test = suite->GetTestInfo(i);
            if (!test->should_run()) continue;
            const char* name = strrchr(test->name(), '/');
            if (!name) continue;
            for (size_t j = 0; j < g_scripts.size(); j++) {
                if (g_scripts[j].name == name + 1) g_scripts[j].selected = true;
            }
        }
        mir_check::ensure_dump_dir();
        // separate parity-only fixtures so their unforced MIR-interpreter
        // runs cannot accidentally inherit an allocating fixture's schedule.
        StressBatch* current[2] = {};
        for (size_t i = 0; i < g_scripts.size(); i++) {
            StressScript& script = g_scripts[i];
            if (!script.selected || !script.emission || script.language != mir_check::LANG_LAMBDA ||
                    g_batch_size == 1) continue;
            size_t group = script.parity_reason && !g_full_stress ? 1 : 0;
            if (!current[group] || (size_t)current[group]->scripts->length == g_batch_size) {
                current[group] = (StressBatch*)calloc(1, sizeof(StressBatch));
                current[group]->scripts = arraylist_new((int)g_batch_size);
                arraylist_append(g_batches, current[group]);
            }
            arraylist_append(current[group]->scripts, &script);
            script.batch = current[group];
        }
    }

    static void TearDownTestSuite() {
        GTEST_LOG_(INFO) << "mir-gc-stress processes completed: count=" << g_process_count
                        << " batch_size=" << g_batch_size << " full=" << g_full_stress;
        for (size_t i = 0; i < g_scripts.size(); i++) {
            for (size_t mode = 0; mode < kModeCount; mode++) test_batch_result_free(&g_scripts[i].runs[mode]);
            g_scripts[i].batch = nullptr;
            g_scripts[i].selected = false;
        }
        if (g_batches) {
            for (int i = 0; i < g_batches->length; i++) {
                StressBatch* batch = (StressBatch*)g_batches->data[i];
                arraylist_free(batch->scripts);
                free(batch);
            }
            arraylist_free(g_batches);
            g_batches = nullptr;
        }
    }
};

TEST_P(MirGcStressTest, MatchesUnstressedRunUnderForcedGc) {
    // GTest copies parameters during discovery; cached runs belong to the
    // canonical corpus entry, which remains stable for the whole suite.
    StressScript& script = g_scripts[GetParam().index];
    const TestBatchResult& reference = get_run(script, REFERENCE);
    GTEST_LOG_(INFO) << "mir-gc-stress reference completed: script=" << script.path
                    << " exit=" << reference.exit_code;
    ASSERT_EQ(reference.exit_code, 0) << script.path << "\n" << reference.output->str;
    if (script.parity_reason && !g_full_stress) {
        GTEST_LOG_(INFO) << "mir-gc-stress backend parity: script=" << script.path
                        << " reason=" << script.parity_reason;
    }
    for (size_t m = 1; m < kModeCount; m++) {
        StressMode mode = (StressMode)m;
        if (!mode_applies(script, mode)) continue;
        SCOPED_TRACE(kModeNames[mode]);
        GTEST_LOG_(INFO) << "mir-gc-stress schedule started: script=" << script.path
                        << " mode=" << kModeNames[mode];
        const TestBatchResult& stressed = get_run(script, mode);
        GTEST_LOG_(INFO) << "mir-gc-stress schedule completed: script=" << script.path
                        << " mode=" << kModeNames[mode] << " exit=" << stressed.exit_code;
        EXPECT_EQ(stressed.exit_code, 0) << script.path << "\n" << stressed.output->str;
        if (stressed.exit_code == 0) {
            EXPECT_TRUE(reference.output->length == stressed.output->length &&
                memcmp(reference.output->str, stressed.output->str, reference.output->length) == 0)
                << "output changed: " << script.path << " mode=" << kModeNames[mode]
                << "; LAMBDA_MIR_GC_BATCH_SIZE=1 isolates the script; "
                   "LAMBDA_MIR_ROOT_MODE=write-through is the rooting bisection oracle."
                << "\n--- expected ---\n" << reference.output->str
                << "\n--- got ---\n" << stressed.output->str;
        }
    }
}

std::string script_name(const ::testing::TestParamInfo<StressScript>& info) {
    return info.param.name;
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MirGcStressTest);

INSTANTIATE_TEST_SUITE_P(Corpus, MirGcStressTest, ::testing::ValuesIn(g_scripts), script_name);

// the sweep's value is its coverage, so an empty corpus must not read green.
TEST(MirGcStressCorpus, IsNotEmpty) {
    EXPECT_FALSE(g_scripts.empty()) << "no scripts collected for the forced-GC sweep";
    load_gc_policy();
    EXPECT_EQ(g_policy_error->length, 0u) << g_policy_error->str;
}

TEST(MirGcStressBatchProtocol, PreservesBinaryOutputAndScriptFailure) {
    const char* paths[] = {"a.ls", "b.ls"};
    const char text[] = "\x01" "BATCH_START a.ls\nfirst\0tail"
        "\x01" "BATCH_END 0\n\x01" "BATCH_START b.ls\nrejected"
        "\x01" "BATCH_END 7\n";
    ShellResult process = {};
    process.stdout_buf = (char*)text;
    process.stdout_len = sizeof(text) - 1;
    TestBatchResult results[2];
    StrBuf* error = strbuf_new();
    EXPECT_TRUE(test_batch_decode(process, paths, 2, results, error)) << error->str;
    EXPECT_EQ(results[0].exit_code, 0);
    ASSERT_NE(results[0].output, nullptr);
    EXPECT_EQ(results[0].output->length, 10u);
    EXPECT_EQ(memcmp(results[0].output->str, "first\0tail", 10), 0);
    EXPECT_EQ(results[1].exit_code, 7);
    ASSERT_NE(results[1].output, nullptr);
    EXPECT_STREQ(results[1].output->str, "rejected");
    for (auto& result : results) test_batch_result_free(&result);
    strbuf_free(error);
}

TEST(MirGcStressBatchProtocol, RejectsMissingDuplicateAndMalformedResults) {
    const char* paths[] = {"a.ls", "b.ls"};
    const char* invalid[] = {
        "\x01" "BATCH_START a.ls\nno completion",
        "\x01" "BATCH_START a.ls\n\x01" "BATCH_END 0\n",
        "\x01" "BATCH_START b.ls\n\x01" "BATCH_END 0\n",
        "\x01" "BATCH_START a.ls\n\x01" "BATCH_END 0\n\x01" "BATCH_START a.ls\n",
        "\x01" "BATCH_START a.ls\n\x01" "BATCH_START b.ls\n",
        "\x01" "BATCH_START a.ls\n\x01" "BATCH_END 0x\n",
        "\x01" "BATCH_START a.ls\n\x01" "BATCH_END 0",
        "unexpected output\n\x01" "BATCH_START a.ls\n"
    };
    for (const char* text : invalid) {
        SCOPED_TRACE(text);
        ShellResult process = {};
        process.stdout_buf = (char*)text;
        process.stdout_len = strlen(text);
        TestBatchResult results[2];
        StrBuf* error = strbuf_new();
        EXPECT_FALSE(test_batch_decode(process, paths, 2, results, error));
        EXPECT_NE(error->length, 0u);
        for (auto& result : results) {
            EXPECT_NE(result.exit_code, 0);
            test_batch_result_free(&result);
        }
        strbuf_free(error);
    }
}

TEST(MirGcStressBatchProtocol, RejectsTeardownFailureAndTruncatedCapture) {
    const char* paths[] = {"a.ls"};
    const char text[] = "\x01" "BATCH_START a.ls\nokay\x01" "BATCH_END 0\n";
    for (int failure = 0; failure < 4; failure++) {
        ShellResult process = {};
        process.stdout_buf = (char*)text;
        process.stdout_len = sizeof(text) - 1;
        process.exit_code = failure == 0 ? 139 : 0;
        process.timed_out = failure == 1;
        process.cancelled = failure == 2;
        process.output_limit_exceeded = failure == 3;
        TestBatchResult result;
        StrBuf* error = strbuf_new();
        EXPECT_FALSE(test_batch_decode(process, paths, 1, &result, error));
        EXPECT_NE(result.exit_code, 0);
        test_batch_result_free(&result);
        strbuf_free(error);
    }
}

}  // namespace
