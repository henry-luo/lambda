// test_mir_gc_stress_gtest.cpp
//
// Forced-GC stress sweep over the MIR emission corpus (MT4 of
// vibe/Lambda_Design_MIR_Emission_Test.md).
//
// MT4 deliberately leaves safepoint currency (Stack API #7) to dynamic oracles
// rather than a static pass: re-implementing CFG liveness would duplicate
// em_finalize_semantic_root_write_back, the hardest code in the emitter, and
// rot alongside it. Instead this binary runs each corpus script under
// precise-only rooting with collection forced at every allocation and freed
// memory poisoned. A value that should be rooted but is not becomes
// unreachable, is collected, and its next use fails deterministically.
//
// The oracle is self-baselining: every stressed run must exit successfully and
// produce byte-identical output to the same script run without stress. That
// needs no golden files, so adding a corpus fixture automatically extends this
// gate, and a fixture whose output legitimately changes cannot silently drift
// out of coverage.
//
// This is behavioral stress over the selected corpus on executed paths, not a
// proof over every CFG path. The design says so explicitly; do not read a green
// run as a rooting proof.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "test_mir_check_helpers.hpp"

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
};

struct StressScript {
    std::string path;
    mir_check::Language language = mir_check::LANG_LAMBDA;
    bool procedural = false;
    std::string name;
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
        scripts.push_back(entry);
    }

    std::vector<mir_check::Fixture> js_fixtures =
        mir_check::discover_fixtures("test/mir/js", ".js");
    for (size_t i = 0; i < js_fixtures.size(); i++) {
        StressScript entry;
        entry.path = js_fixtures[i].script_path;
        entry.language = mir_check::LANG_JS;
        entry.name = "js_" + js_fixtures[i].name;
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
        scripts.push_back(entry);
    }
    return scripts;
}

std::vector<StressScript> g_scripts = collect_scripts();

struct StressMode {
    const char* name;
    bool mir_interp;
    std::vector<std::pair<std::string, std::string> > env;
};

std::vector<StressMode> stress_modes() {
    std::vector<StressMode> modes;

    // collect at every allocation and poison freed memory: a missing root is
    // detected the first time the collector runs while the value is live.
    StressMode jit_forced;
    jit_forced.name = "jit-forced";
    jit_forced.mir_interp = false;
    jit_forced.env.push_back(std::make_pair(std::string("LAMBDA_GC_ROOT_MODE"), std::string("precise-only")));
    jit_forced.env.push_back(std::make_pair(std::string("LAMBDA_GC_FORCE_EVERY"), std::string("1")));
    jit_forced.env.push_back(std::make_pair(std::string("LAMBDA_GC_POISON_FREED"), std::string("1")));
    modes.push_back(jit_forced);

    // deterministic randomized collection reaches interleavings that a
    // collect-always schedule cannot: the seed keeps failures reproducible.
    StressMode randomized;
    randomized.name = "randomized-forced";
    randomized.mir_interp = false;
    randomized.env.push_back(std::make_pair(std::string("LAMBDA_GC_ROOT_MODE"), std::string("precise-only")));
    randomized.env.push_back(std::make_pair(std::string("LAMBDA_GC_FORCE_SEED"), std::string("1592594996")));
    randomized.env.push_back(std::make_pair(std::string("LAMBDA_GC_FORCE_ONE_IN"), std::string("3")));
    randomized.env.push_back(std::make_pair(std::string("LAMBDA_GC_POISON_FREED"), std::string("1")));
    modes.push_back(randomized);

    // the same emitted MIR under the interpreter: separates a rooting bug in
    // the emitted code from one that only manifests through JIT codegen.
    StressMode interp_forced;
    interp_forced.name = "interp-forced";
    interp_forced.mir_interp = true;
    interp_forced.env.push_back(std::make_pair(std::string("LAMBDA_GC_ROOT_MODE"), std::string("precise-only")));
    interp_forced.env.push_back(std::make_pair(std::string("LAMBDA_GC_FORCE_EVERY"), std::string("1")));
    interp_forced.env.push_back(std::make_pair(std::string("LAMBDA_GC_POISON_FREED"), std::string("1")));
    modes.push_back(interp_forced);

    return modes;
}

class MirGcStressTest : public ::testing::TestWithParam<StressScript> {};

TEST_P(MirGcStressTest, MatchesUnstressedRunUnderForcedGc) {
    const StressScript& script = GetParam();

    mir_check::ProcessSpec base;
    base.language = script.language;
    base.procedural = script.procedural;
    // stress runs want no MIR artifacts and no log I/O; --no-log is the master
    // gate for both. Emission-pattern checks are a different binary.
    base.quiet = true;

    mir_check::ProcessResult reference = mir_check::run_lambda_process(script.path, base);
    ASSERT_EQ(reference.exit_code, 0)
        << "reference (unstressed) run failed for " << script.path
        << "\n--- output ---\n" << reference.output;

    std::vector<StressMode> modes = stress_modes();
    for (size_t m = 0; m < modes.size(); m++) {
        const StressMode& mode = modes[m];
        mir_check::ProcessSpec spec = base;
        spec.mir_interp = mode.mir_interp;
        spec.env = mode.env;

        mir_check::ProcessResult stressed = mir_check::run_lambda_process(script.path, spec);

        std::string context =
            "\nscript: " + script.path +
            "\nmode:   " + mode.name +
            "\nReproduce with LAMBDA_GC_ROOT_MODE=precise-only plus that mode's"
            " forcing variables; LAMBDA_MIR_ROOT_MODE=write-through is the"
            " bisection oracle when this fails.";

        EXPECT_EQ(stressed.exit_code, 0)
            << "script died under forced GC, which is what a missing root looks"
               " like once the collector actually runs." << context
            << "\n--- stressed output ---\n" << stressed.output;

        if (stressed.exit_code == 0) {
            EXPECT_EQ(stressed.output, reference.output)
                << "output changed under forced GC: a value that should have been"
                   " rooted was collected while still live." << context
                << "\n--- expected (unstressed) ---\n" << reference.output
                << "\n--- got (stressed) ---\n" << stressed.output;
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
}

}  // namespace
